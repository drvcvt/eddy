#include "editorwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "toolcontroller.h"
#include "exporter.h"
#include "boltsnapipc.h"
#include "videoexporter.h"
#include "selectionhandles.h"
#include "undocommands.h"
#include "items/textitem.h"
#include "redactbar.h"
#include "toast.h"
#include "dragpill.h"
#include "redactocrcontroller.h"
#include "items/redactitem.h"
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <cstdio>
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QMediaPlayer>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QClipboard>
#include <QMimeData>
#include <QApplication>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QBrush>
#include <QPen>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QUrl>
#include <QTimer>
#include <QThread>
#include <QPointer>
#include <limits>

namespace eddy {

bool imageSaveUsesShelfReturn(const CliOptions &cli) {
    return !cli.output.toFile
        && !cli.output.toStdout
        && cli.output.saveDir.isEmpty();
}

namespace {

MediaDocument imageDocument(const QImage &image) {
    MediaDocument doc;
    doc.kind = MediaKind::Image;
    doc.image = image;
    return doc;
}

QImage toolBackgroundFor(const MediaDocument &media) {
    if (media.kind == MediaKind::Image)
        return media.image;
    QImage bg(media.nativeSize(), QImage::Format_ARGB32_Premultiplied);
    // Video blur redaction cannot sample a moving frame yet. Use black as the
    // fail-safe source so the existing Redact tool never becomes a transparent
    // no-op on videos; dynamic blur/OCR can replace this with a frame provider.
    bg.fill(Qt::black);
    return bg;
}

QString formatTime(qint64 ms) {
    const qint64 total = qMax<qint64>(0, ms / 1000);
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(m)
        .arg(s, 2, 10, QLatin1Char('0'));
}

}

EditorWindow::EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : EditorWindow(imageDocument(image), cfg, cli, parent) {}

EditorWindow::EditorWindow(const MediaDocument &media, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : QWidget(parent), m_media(media), m_bg(toolBackgroundFor(media)), m_cfg(cfg), m_cli(cli) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setWindowTitle("eddy");
    setObjectName("EditorRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    m_scene = new QGraphicsScene(this);
    const QSize native = m_media.nativeSize();
    m_scene->setSceneRect(0,0,native.width(),native.height());
    if (isVideo()) {
        auto *bgItem = m_scene->addRect(
            QRectF(QPointF(0, 0), QSizeF(native)),
            QPen(Qt::NoPen),
            QBrush(QColor("#050505")));
        bgItem->setZValue(-1000);
        m_backgroundItem = bgItem;
    } else {
        auto *bgItem = m_scene->addPixmap(QPixmap::fromImage(m_bg));
        bgItem->setZValue(-1000);
        m_backgroundItem = bgItem;
    }

    m_undo = new QUndoStack(this);
    m_tools = new ToolController(m_scene, m_undo, m_bg, this);
    m_tools->setTool(toolFromName(cfg.defaultTool));
    m_tools->setColor(cfg.strokeColor);
    m_tools->setWidth(cfg.lineWidth);

    m_canvas = new Canvas(m_scene, m_tools, this);
    m_toolbar = new Toolbar(this);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(0);
    lay->addWidget(m_toolbar);
    lay->addWidget(m_canvas, 1);
    if (isVideo())
        lay->addWidget(createPlaybackBar());

    connect(m_toolbar, &Toolbar::toolChosen, m_tools, &ToolController::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, m_tools, &ToolController::setColor);
    connect(m_toolbar, &Toolbar::saveRequested, this, &EditorWindow::save);
    connect(m_toolbar, &Toolbar::copyRequested, this, &EditorWindow::copy);
    connect(m_toolbar, &Toolbar::sendToShelfRequested, this, &EditorWindow::sendToShelf);
    connect(m_tools, &ToolController::toolChanged, m_toolbar, &Toolbar::syncTool);
    connect(m_toolbar, &Toolbar::widthChosen, m_tools, &ToolController::setWidth);
    connect(m_toolbar, &Toolbar::undoRequested, this, &EditorWindow::doUndo);
    connect(m_toolbar, &Toolbar::redoRequested, this, &EditorWindow::doRedo);
    connect(m_undo, &QUndoStack::canUndoChanged, m_toolbar, &Toolbar::setUndoEnabled);
    connect(m_undo, &QUndoStack::canRedoChanged, m_toolbar, &Toolbar::setRedoEnabled);
    connect(m_toolbar, &Toolbar::eyedropperRequested, this, [this]{
        m_canvas->startEyedropper();
        if (m_toast) m_toast->showMessage(QStringLiteral("Click to pick a colour \xC2\xB7 Esc to cancel"));
    });
    connect(m_canvas, &Canvas::colorPicked, this, [this](const QColor &c){
        m_tools->setColor(c);
        m_toolbar->setSwatchColor(c);
    });
    m_toolbar->setSwatchColor(cfg.strokeColor);   // disc starts in the real stroke colour
    if (isVideo()) {
        m_videoExportTimer = new QTimer(this);
        m_videoExportTimer->setSingleShot(true);
        connect(m_videoExportTimer, &QTimer::timeout, this, &EditorWindow::startVideoExportCache);
        connect(m_undo, &QUndoStack::indexChanged, this, [this](int){ onVideoContentChanged(); });
    }
    m_handles = new SelectionHandles(m_scene, m_undo, this);
    m_ocr = new RedactOcrController(m_bg, {m_cfg.ocrLang, m_cfg.ocrPsm}, this);
    m_redactBar = new RedactBar(m_canvas->viewport());
    m_redactBar->hide();
    m_toast = new Toast(this);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshRedactBar);
    connect(m_scene, &QGraphicsScene::changed, this, [this](const QList<QRectF> &){
        positionRedactBar();
        if (!isVideo() || !hasVideoAnnotations() || m_renderingVideoOverlay)
            return;
        if (m_player && m_player->playbackState() == QMediaPlayer::PlayingState)
            return;
        onVideoContentChanged();
    });
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionRedactBar);
    connect(m_redactBar, &RedactBar::modeChosen, this, &EditorWindow::onRedactModeChosen);
    connect(m_ocr, &RedactOcrController::noTextDetected, this,
            [this]{ m_toast->showMessage(QStringLiteral("No text detected")); });
    connect(m_ocr, &RedactOcrController::ocrFailed, this,
            [this](const QString &msg){ m_toast->showMessage(QStringLiteral("OCR failed: ") + msg); });
    connect(m_handles, &SelectionHandles::resizeFinished, this, [this](QGraphicsItem *it){
        if (auto *r = dynamic_cast<RedactItem*>(it); r && RedactItem::isOcr(r->mode()))
            m_ocr->detectFor(r);   // re-run detection for the new geometry
    });
    // Drag-out pill lives in a strip BELOW the canvas so it never covers the image.
    m_dragPill = new DragPill(this);
    if (isVideo()) {
        m_dragPill->setFileProvider([this]{
            FileDragPayload payload;
            payload.path = videoDeliveryPath();
            // Qt fallback stays URL-only so it never materializes full video bytes
            // in-process. On Wayland, DragPill uses the Boltsnap-style native
            // data source and streams this MIME through the compositor pipe.
            // Boltsnap streams bytes through a native Wayland pipe; Qt's
            // QMimeData API cannot provide the same detached writer safely.
            payload.mimeType = mediaMimeTypeForPath(payload.path);
            payload.includeBytesInQtMime = false;
            payload.preview = {};
            payload.removeAfterUse = false;
            return payload;
        });
    } else {
        m_dragPill->setImageProvider([this]{ return exportComposite(); });
    }
    auto *footer = new QWidget(this);
    footer->setObjectName("Footer");
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(0, 6, 0, 6);
    fl->addStretch(1);
    fl->addWidget(m_dragPill);
    fl->addStretch(1);
    lay->addWidget(footer);
    m_canvas->setAnimationsEnabled(cfg.animations);
    m_toolbar->setAnimationsEnabled(cfg.animations);
    m_tools->setAnimationsEnabled(cfg.animations);
    // Sync the toolbar to the configured default explicitly: the earlier
    // m_tools->setTool() ran before this connect existed (its toolChanged was
    // dropped), and setTool only emits on a *change* — so a default of "arrow"
    // (the controller's default) would emit nothing at all. Keep this call.
    m_toolbar->syncTool(toolFromName(cfg.defaultTool));
    if (cfg.animations) setWindowOpacity(0.0);   // entrance fade starts transparent

    // size to image, capped
    const int maxW = 1700, maxH = 1000;
    resize(qMin(native.width(), maxW), qMin(native.height()+40, maxH));
    m_toolbar->adjustSize();
    const int barW = m_toolbar->sizeHint().width();
    const int barH = m_toolbar->sizeHint().height();
    setMinimumSize(qMax(barW, 360), barH + 120);   // bar never clipped; usable image strip
}

void EditorWindow::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (m_shown) return;
    m_shown = true;
    if (isVideo()) scheduleVideoLoad();
    if (!m_cfg.animations) { setWindowOpacity(1.0); return; }
    auto *a = new QPropertyAnimation(this, "windowOpacity", this);
    a->setDuration(150); a->setStartValue(0.0); a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void EditorWindow::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updateCompactMode();
}

void EditorWindow::updateCompactMode() {
    const int barH = m_toolbar->sizeHint().height();
    const bool compact = height() < barH + 90;       // too short for strip + image
    if (compact == m_compact) return;
    m_compact = compact;
    auto *lay = static_cast<QVBoxLayout*>(layout());
    if (compact) {
        // Detach the bar from the layout (removeWidget — setParent alone leaves it
        // managed) and float it as a top auto-hide overlay.
        lay->removeWidget(m_toolbar);
        m_toolbar->setParent(this);                   // keep it a child for geometry/raise
        m_toolbar->raise();
        m_toolbar->setGeometry(0, 0, width(), barH);
        m_toolbar->hide();                            // canvas gets full height; bar on hover
        setMouseTracking(true);
        m_canvas->setMouseTracking(true);
    } else {
        // Re-dock the bar at the top of the layout.
        lay->insertWidget(0, m_toolbar);
        m_toolbar->show();
        setMouseTracking(false);
        m_canvas->setMouseTracking(false);
    }
}

QWidget *EditorWindow::createPlaybackBar() {
    auto *bar = new QWidget(this);
    bar->setObjectName("PlaybackBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(10, 5, 10, 5);
    lay->setSpacing(8);

    m_playButton = new QToolButton(bar);
    m_playButton->setText(QString::fromUtf8("\xE2\x96\xB6")); // ▶
    m_playButton->setObjectName("PlaybackPlay");
    m_playButton->setAutoRaise(true);
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setFixedSize(34, 28);
    m_playButton->setToolTip(QStringLiteral("Play / Pause"));
    m_positionSlider = new QSlider(Qt::Horizontal, bar);
    m_positionSlider->setObjectName("PlaybackPosition");
    m_positionSlider->setRange(
        0,
        int(qMin<qint64>(qMax<qint64>(0, m_media.video.durationMs), std::numeric_limits<int>::max())));
    m_timeLabel = new QLabel(QStringLiteral("0:00 / ") + formatTime(m_media.video.durationMs), bar);
    m_timeLabel->setObjectName("PlaybackTime");

    lay->addWidget(m_playButton);
    lay->addWidget(m_positionSlider, 1);
    lay->addWidget(m_timeLabel);

    connect(m_playButton, &QToolButton::clicked, this, [this]{
        ensureVideoPlayer();
        if (!m_player) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    });
    return bar;
}

void EditorWindow::scheduleVideoLoad() {
    if (!isVideo() || m_videoLoadQueued || m_player) return;
    m_videoLoadQueued = true;
    // Small delay lets the frameless editor paint before Qt Multimedia opens the
    // file. The scene dimensions are already known from ffprobe, so no UX state
    // depends on synchronously constructing the backend.
    QTimer::singleShot(75, this, [this]{
        m_videoLoadQueued = false;
        ensureVideoPlayer();
    });
}

void EditorWindow::ensureVideoPlayer() {
    if (!isVideo() || m_player) return;
    if (!m_videoItem) {
        auto *videoItem = new QGraphicsVideoItem;
        videoItem->setSize(QSizeF(m_media.nativeSize()));
        videoItem->setZValue(-1000);
        m_scene->addItem(videoItem);
        if (m_backgroundItem) {
            m_scene->removeItem(m_backgroundItem);
            delete m_backgroundItem;
        }
        m_videoItem = videoItem;
        m_backgroundItem = m_videoItem;
    }
    m_player = new QMediaPlayer(this);
    m_player->setVideoOutput(m_videoItem);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state){
        if (m_playButton)
            m_playButton->setText(state == QMediaPlayer::PlayingState
                ? QString::fromUtf8("\xE2\x8F\xB8") : QString::fromUtf8("\xE2\x96\xB6")); // ⏸ / ▶
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration){
        if (!m_positionSlider) return;
        m_positionSlider->setRange(0, int(qMin<qint64>(duration, std::numeric_limits<int>::max())));
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(m_player->position()) + QStringLiteral(" / ") + formatTime(duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos){
        if (m_positionSlider && !m_positionSlider->isSliderDown())
            m_positionSlider->setValue(int(qMin<qint64>(pos, std::numeric_limits<int>::max())));
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") + formatTime(m_player->duration()));
    });
    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int value){
        if (m_player) m_player->setPosition(value);
    });
    connect(m_positionSlider, &QSlider::sliderReleased, this, [this]{
        if (m_player && m_positionSlider) m_player->setPosition(m_positionSlider->value());
    });
    m_player->setSource(QUrl::fromLocalFile(m_media.path));
}

void EditorWindow::mouseMoveEvent(QMouseEvent *e) {
    if (m_compact) {
        const int barH = m_toolbar->sizeHint().height();
        if (e->position().y() <= 12) {
            m_toolbar->setGeometry(0, 0, width(), barH);
            m_toolbar->raise(); m_toolbar->show();
        } else if (e->position().y() > barH + 8) {
            m_toolbar->hide();
        }
    }
    QWidget::mouseMoveEvent(e);
}

RedactItem *EditorWindow::selectedRedact() const {
    const auto sel = m_scene->selectedItems();
    if (sel.size() != 1) return nullptr;
    return dynamic_cast<RedactItem *>(sel.first());
}

void EditorWindow::refreshRedactBar() {
    RedactItem *r = selectedRedact();
    if (!r) { m_redactBar->hide(); return; }
    m_redactBar->setMode(r->mode());
    m_redactBar->adjustSize();
    m_redactBar->show();
    m_redactBar->raise();
    positionRedactBar();
}

void EditorWindow::positionRedactBar() {
    if (m_redactBar->isHidden()) return;
    RedactItem *r = selectedRedact();
    if (!r) { m_redactBar->hide(); return; }
    const QRectF rc = r->rect();
    const QPoint topCenter = m_canvas->mapFromScene(QPointF(rc.center().x(), rc.top()));
    const QSize vp = m_canvas->viewport()->size();
    int x = topCenter.x() - m_redactBar->width() / 2;
    int y = topCenter.y() - m_redactBar->height() - 8;
    x = qBound(4, x, qMax(4, vp.width() - m_redactBar->width() - 4));
    if (y < 4) y = topCenter.y() + 8;     // no room above -> just below the top edge
    m_redactBar->move(x, y);
}


void EditorWindow::onRedactModeChosen(RedactMode m) {
    RedactItem *r = selectedRedact();
    if (!r) return;
    const RedactMode before = r->mode();
    if (before != m) m_undo->push(new SetRedactModeCommand(r, before, m));
    if (RedactItem::isOcr(m)) m_ocr->detectFor(r);   // detecting -> whole-region cover until result
    m_redactBar->setMode(m);
    positionRedactBar();
}

void EditorWindow::doUndo() { m_ocr->cancel(); m_undo->undo(); refreshRedactBar(); }
void EditorWindow::doRedo() { m_ocr->cancel(); m_undo->redo(); refreshRedactBar(); }

QImage EditorWindow::exportComposite() {
    if (isVideo())
        return renderAnnotationOverlay();
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the image
    return renderToImage(*m_scene, m_bg.size());
}

QImage EditorWindow::renderAnnotationOverlay() {
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the video
    const bool hadBackground = m_backgroundItem != nullptr;
    const bool wasVisible = hadBackground && m_backgroundItem->isVisible();
    m_renderingVideoOverlay = true;
    const int renderGeneration = ++m_videoOverlayRenderGeneration;
    if (hadBackground) m_backgroundItem->setVisible(false);
    QImage overlay = renderToImage(*m_scene, m_media.nativeSize());
    if (hadBackground) m_backgroundItem->setVisible(wasVisible);
    QTimer::singleShot(50, this, [this, renderGeneration]{
        if (renderGeneration == m_videoOverlayRenderGeneration)
            m_renderingVideoOverlay = false;
    });
    return overlay;
}

bool EditorWindow::hasVideoAnnotations() const {
    return isVideo() && m_undo && !m_undo->isClean();
}

QString EditorWindow::videoDeliveryPath() {
    if (!hasVideoAnnotations())
        return m_media.path;
    if (m_cachedVideoRevision == m_videoRevision && QFileInfo::exists(m_cachedVideoPath))
        return m_cachedVideoPath;
    scheduleVideoExportCache(0);
    if (m_toast)
        m_toast->showMessage(QStringLiteral("Preparing video export…"));
    return {};
}

void EditorWindow::onVideoContentChanged() {
    if (!isVideo()) return;
    ++m_videoRevision;
    if (hasVideoAnnotations())
        scheduleVideoExportCache();
}

void EditorWindow::scheduleVideoExportCache(int delayMs) {
    if (!isVideo() || !hasVideoAnnotations() || !m_videoExportTimer) return;
    m_videoExportTimer->start(qMax(0, delayMs));
}

QString EditorWindow::createVideoTempPath() const {
    const QString suffix = QFileInfo(m_media.path).suffix().isEmpty()
        ? QStringLiteral("mp4")
        : QFileInfo(m_media.path).suffix();
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/eddy-video-XXXXXX.") + suffix);
    tmp.setAutoRemove(false);
    if (!tmp.open()) {
        std::fprintf(stderr, "eddy: cannot create temporary video\n");
        return {};
    }
    const QString path = tmp.fileName();
    tmp.close();
    return path;
}

void EditorWindow::startVideoExportCache() {
    if (!isVideo() || !hasVideoAnnotations()) return;
    if (m_videoExportInProgress) {
        m_videoExportPending = true;
        return;
    }

    const QString path = createVideoTempPath();
    if (path.isEmpty()) return;

    const int revision = m_videoRevision;
    const VideoExportRequest request{m_media.path, path, renderAnnotationOverlay()};
    m_videoExportInProgress = true;

    QPointer<EditorWindow> receiver(this);
    auto *thread = QThread::create([receiver, revision, path, request] {
        const DeliverResult result = writeVideoWithOverlay(request);
        QMetaObject::invokeMethod(qApp, [receiver, revision, path, result] {
            if (receiver)
                receiver->finishVideoExportCache(revision, path, result);
            else
                QFile::remove(path);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void EditorWindow::finishVideoExportCache(int revision, const QString &path, const DeliverResult &result) {
    m_videoExportInProgress = false;
    const bool current = result.ok && hasVideoAnnotations() && revision == m_videoRevision;
    if (current) {
        m_cachedVideoPath = path;
        m_cachedVideoRevision = revision;
    } else {
        QFile::remove(path);
        if (!result.ok && revision == m_videoRevision)
            std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
    }

    const bool needsFreshExport = hasVideoAnnotations() && m_cachedVideoRevision != m_videoRevision;
    if (m_videoExportPending || needsFreshExport) {
        m_videoExportPending = false;
        scheduleVideoExportCache(100);
    }
}

DeliverResult EditorWindow::exportVideoToFile(const QString &path) {
    if (!hasVideoAnnotations()) {
        const QFileInfo src(m_media.path);
        const QFileInfo dst(path);
        if (src.canonicalFilePath() == dst.canonicalFilePath())
            return {true, {}};
        if (dst.exists() && !QFile::remove(path))
            return {false, QStringLiteral("cannot replace ") + path};
        if (!QFile::copy(m_media.path, path))
            return {false, QStringLiteral("cannot copy video to ") + path};
        return {true, {}};
    }
    return writeVideoWithOverlay({m_media.path, path, renderAnnotationOverlay()});
}

void EditorWindow::copyVideoFile(const QString &path) {
    if (path.isEmpty()) return;
    QApplication::clipboard()->setMimeData(makeUrlDropMime(path));
}

void EditorWindow::saveVideo() {
    QString path;
    if (m_cli.output.toFile) {
        path = m_cli.output.filePath;
    } else if (m_cli.output.toStdout) {
        std::fprintf(stderr, "eddy: video export to stdout is not supported\n");
    } else if (!m_cli.output.saveDir.isEmpty()) {
        const QString suffix = QFileInfo(m_media.path).suffix().isEmpty()
            ? QStringLiteral("mp4")
            : QFileInfo(m_media.path).suffix();
        path = QDir(m_cli.output.saveDir).filePath(
            QStringLiteral("eddy-")
            + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss")
            + QStringLiteral(".") + suffix);
    }

    QString copiedPath;
    if (!path.isEmpty()) {
        auto res = exportVideoToFile(path);
        if (!res.ok)
            std::fprintf(stderr, "eddy: %s\n", qPrintable(res.error));
        else
            copiedPath = path;
    }
    if (m_cfg.copyOnSave || path.isEmpty()) {
        if (copiedPath.isEmpty())
            copiedPath = videoDeliveryPath();
        copyVideoFile(copiedPath);
    }
    if (m_cfg.earlyExit) close();
}

bool EditorWindow::postImageToShelf(const QImage &img, bool showSuccessToast) {
    const DeliverResult res = sendPngToBoltsnapShelf(encodePng(img), QStringLiteral("eddy"));
    if (!res.ok) {
        std::fprintf(stderr, "eddy: %s\n", qPrintable(res.error));
        if (m_toast)
            m_toast->showMessage(QStringLiteral("Boltsnap shelf unavailable"));
        return false;
    }
    if (showSuccessToast && m_toast)
        m_toast->showMessage(QStringLiteral("Sent to Boltsnap shelf"));
    return true;
}

void EditorWindow::save() {
    if (isVideo()) { saveVideo(); return; }
    QImage img = exportComposite();
    if (imageSaveUsesShelfReturn(m_cli)) {
        const bool sent = postImageToShelf(img, true);
        if (m_cfg.copyOnSave || !sent)
            QApplication::clipboard()->setImage(img);
        if (m_cfg.earlyExit) close();
        return;
    }

    // Write a file only when an explicit target was requested: -o FILE, -o -,
    // or --save-dir DIR. The default image Save path returns the composite to
    // Boltsnap's shelf above; no screenshot file is written unless requested.
    QString path;
    if (m_cli.output.toFile)            path = m_cli.output.filePath;
    else if (m_cli.output.toStdout)     path = QStringLiteral("-");
    else if (!m_cli.output.saveDir.isEmpty())
        path = QDir(m_cli.output.saveDir).filePath(
                   "eddy-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png");

    if (!path.isEmpty()) {
        auto res = writePng(img, path);
        if (!res.ok) std::fprintf(stderr, "eddy: %s\n", qPrintable(res.error));
    }
    if (m_cfg.copyOnSave || path.isEmpty())
        QApplication::clipboard()->setImage(img);   // always at least copy
    if (m_cfg.earlyExit) close();
}

void EditorWindow::sendToShelf() {
    if (isVideo()) {
        if (m_toast)
            m_toast->showMessage(QStringLiteral("Shelf return supports images"));
        return;
    }
    postImageToShelf(exportComposite(), true);
}

void EditorWindow::copy() {
    if (isVideo()) {
        copyVideoFile(videoDeliveryPath());
        return;
    }
    QApplication::clipboard()->setImage(exportComposite());
}

void EditorWindow::keyPressEvent(QKeyEvent *e) {
    if (m_canvas->eyedropperActive()) {           // eyedropper swallows keys; Esc cancels
        if (e->key() == Qt::Key_Escape) m_canvas->cancelEyedropper();
        e->accept();
        return;
    }
    // While a text annotation is being edited, don't hijack letter keys as tool
    // hotkeys — let them type into the text box. Esc commits and leaves editing.
    QGraphicsItem *fi = m_scene->focusItem();
    if (fi && fi->type() == TextItem::Type
        && (static_cast<QGraphicsTextItem *>(fi)->textInteractionFlags() & Qt::TextEditorInteraction)) {
        if (e->key() == Qt::Key_Escape) { m_scene->clearFocus(); e->accept(); return; }
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
        case Qt::Key_A: m_tools->setTool(ToolType::Arrow); break;
        case Qt::Key_P: m_tools->setTool(ToolType::Pen); break;
        case Qt::Key_R: m_tools->setTool(ToolType::Rect); break;
        case Qt::Key_E: m_tools->setTool(ToolType::Ellipse); break;
        case Qt::Key_H: m_tools->setTool(ToolType::Highlight); break;
        case Qt::Key_T: m_tools->setTool(ToolType::Text); break;
        case Qt::Key_X: m_tools->setTool(ToolType::Redact); break;
        case Qt::Key_M: m_tools->setTool(ToolType::Move); break;
        case Qt::Key_Z: if (e->modifiers() & Qt::ControlModifier) {
                            (e->modifiers() & Qt::ShiftModifier) ? doRedo() : doUndo();
                        } break;
        case Qt::Key_C: if (e->modifiers() & Qt::ControlModifier) copy(); break;
        case Qt::Key_S: if (e->modifiers() & Qt::ControlModifier) save(); break;
        case Qt::Key_Return: case Qt::Key_Enter: save(); break;
        case Qt::Key_Delete: case Qt::Key_Backspace: {
            const auto sel = m_scene->selectedItems();
            bool removed = false;
            for (QGraphicsItem *it : sel) {
                if (it->zValue() <= -1000) continue;     // never the background
                if (auto *r = dynamic_cast<RedactItem *>(it)) m_ocr->forget(r);
                m_undo->push(new RemoveItemCommand(m_scene, it));
                removed = true;
            }
            if (!removed) QWidget::keyPressEvent(e);
            break;
        }
        case Qt::Key_Escape: close(); break;
        default: QWidget::keyPressEvent(e);
    }
}

}

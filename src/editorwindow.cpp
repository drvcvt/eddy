#include "editorwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "toolcontroller.h"
#include "exporter.h"
#include "boltsnapipc.h"
#include "videoexporter.h"
#include "videotimeline.h"
#include "selectionhandles.h"
#include "undocommands.h"
#include "items/textitem.h"
#include "redactbar.h"
#include "textbar.h"
#include "spotlightbar.h"
#include "items/spotlightitem.h"
#include "toast.h"
#include "dragpill.h"
#include "redactocrcontroller.h"
#include "items/redactitem.h"
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <cstdio>
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QAudioOutput>
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
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
#include <QScreen>
#include <QSettings>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

namespace eddy {

SaveRoute saveRoute(const CliOptions &cli, const Config &cfg) {
    if (cli.output.toFile || cli.output.toStdout || !cli.output.saveDir.isEmpty())
        return SaveRoute::ExplicitOutput;
    if (cli.boltsnapCardId)
        return SaveRoute::BoltsnapCard;
    if (!cfg.saveDir.isEmpty())
        return SaveRoute::ConfigDirectory;
    return SaveRoute::Shelf;
}

namespace {

#ifdef Q_OS_WIN
void applyWindowsTitleBarTheme(QWidget *window, bool dark) {
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    const BOOL darkMode = dark ? TRUE : FALSE;
    constexpr DWORD immersiveDarkMode = 20;
    constexpr DWORD immersiveDarkModeBefore20H1 = 19;
    if (FAILED(DwmSetWindowAttribute(handle, immersiveDarkMode,
                                     &darkMode, sizeof(darkMode)))) {
        DwmSetWindowAttribute(handle, immersiveDarkModeBefore20H1,
                              &darkMode, sizeof(darkMode));
    }

    constexpr DWORD borderColorAttribute = 34;
    constexpr DWORD captionColorAttribute = 35;
    constexpr DWORD textColorAttribute = 36;
    const COLORREF borderColor = dark ? RGB(0x2a, 0x2a, 0x2a) : RGB(0xde, 0xde, 0xde);
    const COLORREF captionColor = dark ? RGB(0x12, 0x12, 0x12) : RGB(0xfa, 0xfa, 0xfa);
    const COLORREF textColor = dark ? RGB(0xec, 0xec, 0xec) : RGB(0x1a, 0x1a, 0x1a);
    DwmSetWindowAttribute(handle, borderColorAttribute, &borderColor, sizeof(borderColor));
    DwmSetWindowAttribute(handle, captionColorAttribute, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(handle, textColorAttribute, &textColor, sizeof(textColor));
}
#endif

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

QString formatPreciseTime(qint64 ms) {
    ms = qMax<qint64>(0, ms);
    const qint64 totalSeconds = ms / 1000;
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds % 3600) / 60;
    const qint64 s = totalSeconds % 60;
    const qint64 millis = ms % 1000;
    if (h > 0)
        return QStringLiteral("%1:%2:%3.%4").arg(h).arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0')).arg(millis, 3, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2.%3").arg(m).arg(s, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

QPoint contextBarPosition(const QRect &item, const QSize &bar, const QSize &viewport) {
    constexpr int margin = 4;
    constexpr int gap = 8;
    const int maxX = qMax(margin, viewport.width() - bar.width() - margin);
    const int maxY = qMax(margin, viewport.height() - bar.height() - margin);
    const int x = qBound(margin, item.center().x() - bar.width() / 2, maxX);
    const int above = item.top() - bar.height() - gap;
    const int below = item.bottom() + 1 + gap;
    if (above >= margin) return {x, above};
    if (below <= maxY) return {x, below};

    const int topY = qBound(margin, above, maxY);
    const int bottomY = qBound(margin, below, maxY);
    const auto overlap = [&](int y) {
        const QRect covered(QPoint(x, y), bar);
        const QRect intersection = covered.intersected(item);
        return intersection.width() * intersection.height();
    };
    return {x, overlap(bottomY) <= overlap(topY) ? bottomY : topY};
}

DeliverResult copyVideoAtomically(const QString &sourcePath, const QString &destinationPath) {
    const QFileInfo sourceInfo(sourcePath);
    const QFileInfo destinationInfo(destinationPath);
    if (!sourceInfo.isFile())
        return {false, QStringLiteral("video source is unavailable")};
    if (sourceInfo.canonicalFilePath() == destinationInfo.canonicalFilePath()
        && !sourceInfo.canonicalFilePath().isEmpty())
        return {true, {}};
    QFile source(sourcePath);
    QSaveFile destination(destinationPath);
    if (!source.open(QIODevice::ReadOnly))
        return {false, source.errorString()};
    if (!destination.open(QIODevice::WriteOnly))
        return {false, destination.errorString()};
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (;;) {
        const qint64 count = source.read(buffer.data(), buffer.size());
        if (count < 0) return {false, source.errorString()};
        if (count == 0) break;
        if (destination.write(buffer.constData(), count) != count)
            return {false, destination.errorString()};
    }
    if (!destination.commit())
        return {false, destination.errorString()};
    return {true, {}};
}

}

EditorWindow::EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : EditorWindow(imageDocument(image), cfg, cli, parent) {}

EditorWindow::~EditorWindow() {
    QObject::disconnect(m_scene, nullptr, this, nullptr);
    QObject::disconnect(m_undo, nullptr, this, nullptr);
    if (!m_cachedVideoPath.isEmpty() && !m_clipboardVideoPaths.contains(m_cachedVideoPath)
        && !m_videoIpcPaths.contains(m_cachedVideoPath))
        QFile::remove(m_cachedVideoPath);
}

EditorWindow::EditorWindow(const MediaDocument &media, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : QWidget(parent), m_media(media), m_bg(toolBackgroundFor(media)), m_cfg(cfg), m_cli(cli) {
#ifdef Q_OS_WIN
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                   | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint
                   | Qt::WindowStaysOnTopHint);
#else
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
#endif
    setWindowTitle("eddy");
    setObjectName("EditorRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    m_scene = new QGraphicsScene(this);
    connect(QApplication::clipboard(), &QClipboard::changed, this, [this] {
        QSet<QString> referenced;
        const QMimeData *mime = QApplication::clipboard()->mimeData();
        if (mime) {
            for (const QUrl &url : mime->urls())
                referenced.insert(url.toLocalFile());
        }
        for (const QString &path : std::as_const(m_clipboardVideoPaths)) {
            if (!referenced.contains(path) && path != m_cachedVideoPath
                && !m_videoIpcPaths.contains(path)) {
                QFile::remove(path);
            }
        }
        m_clipboardVideoPaths.intersect(referenced);
    });
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
        bgItem->setTransformationMode(Qt::SmoothTransformation);
        bgItem->setZValue(-1000);
        m_backgroundItem = bgItem;
    }

    m_undo = new QUndoStack(this);
    m_tools = new ToolController(m_scene, m_undo, m_bg, this);
    m_tools->setTool(toolFromName(cfg.defaultTool));
    m_tools->setColor(cfg.strokeColor);
    m_tools->setWidth(cfg.lineWidth);
    m_tools->setTextFont(cfg.textFont);

    m_canvas = new Canvas(m_scene, m_tools, this);
    m_toolbar = new Toolbar(this);
    m_dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    if (isVideo()) m_trimOutMs = m_media.video.durationMs;

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(0);
    lay->addWidget(m_toolbar);
    lay->addWidget(m_canvas, 1);
    if (isVideo())
        lay->addWidget(createPlaybackBar());

    connect(m_toolbar, &Toolbar::toolChosen, m_tools, &ToolController::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, this, [this](const QColor &color){
        m_tools->setColor(color);
        updateSelectedText([color](TextItem *text){ text->setAnnotationColor(color); });
    });
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
    connect(m_toolbar, &Toolbar::themeToggleRequested, this, &EditorWindow::toggleTheme);
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
    m_textBar = new TextBar(m_canvas->viewport());
    m_textBar->hide();
    m_spotlightBar = new SpotlightBar(m_canvas->viewport());
    m_spotlightBar->hide();
    m_toast = new Toast(this);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshRedactBar);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshTextBar);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshSpotlightBar);
    connect(m_spotlightBar, &SpotlightBar::shapeChosen, this, [this](SpotlightShape shape) {
        if (auto *spot = selectedSpotlight(); spot && spot->spotlightShape() != shape)
            m_undo->push(new SetSpotlightStyleCommand(
                spot, spot->spotlightShape(), spot->intensity(), shape, spot->intensity()));
    });
    connect(m_spotlightBar, &SpotlightBar::intensityChosen, this, [this](int level) {
        if (auto *spot = selectedSpotlight(); spot && spot->intensity() != level)
            m_undo->push(new SetSpotlightStyleCommand(
                spot, spot->spotlightShape(), spot->intensity(), spot->spotlightShape(), level));
    });
    connect(m_scene, &QGraphicsScene::changed, this, [this](const QList<QRectF> &){
        positionRedactBar();
        positionTextBar();
        positionSpotlightBar();
    });
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionRedactBar);
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionTextBar);
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionSpotlightBar);
    connect(m_redactBar, &RedactBar::modeChosen, this, &EditorWindow::onRedactModeChosen);
    connect(m_textBar, &TextBar::sizeChosen, this, [this](qreal size){
        updateSelectedText([size](TextItem *text){ QFont f=text->font(); f.setPointSizeF(size); text->setFont(f); });
    });
    connect(m_textBar, &TextBar::boldChosen, this, [this](bool bold){
        updateSelectedText([bold](TextItem *text){ QFont f=text->font(); f.setBold(bold); text->setFont(f); });
    });
    connect(m_textBar, &TextBar::alignmentChosen, this, [this](Qt::Alignment alignment){
        updateSelectedText([alignment](TextItem *text){ text->setAlignment(alignment); });
    });
    connect(m_textBar, &TextBar::styleChosen, this, [this](TextLabelStyle style){
        updateSelectedText([style](TextItem *text){ text->setLabelStyle(style); });
    });
    connect(m_ocr, &RedactOcrController::noTextDetected, this,
            [this]{ m_toast->showMessage(QStringLiteral("No text detected")); });
    connect(m_ocr, &RedactOcrController::contentChanged,
            this, &EditorWindow::onVideoContentChanged);
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
    m_tools->setAnimationsEnabled(cfg.animations);
    // Sync the toolbar to the configured default explicitly: the earlier
    // m_tools->setTool() ran before this connect existed (its toolChanged was
    // dropped), and setTool only emits on a *change* — so a default of "arrow"
    // (the controller's default) would emit nothing at all. Keep this call.
    m_toolbar->syncTool(toolFromName(cfg.defaultTool));
    if (cfg.animations) setWindowOpacity(0.0);   // entrance fade starts transparent

    // Size the canvas to the image at 100%; only oversized media starts fitted.
    const int maxW = 1700, maxH = 1000;
    const int barH = m_toolbar->sizeHint().height();
    const int chromeH = lay->sizeHint().height() - m_canvas->sizeHint().height();
    const int minW = qMax(760, m_toolbar->sizeHint().width());
    setMinimumSize(minW, barH + 120);
    resize(qMin(qMax(native.width(), minW), maxW), qMin(native.height() + chromeH, maxH));
}

void EditorWindow::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
#ifdef Q_OS_WIN
    applyWindowsTitleBarTheme(this, m_dark);
#endif
    if (m_shown) return;
    m_shown = true;
    m_canvas->resetZoom();
    const QSize viewport = m_canvas->viewport()->size();
    const QSize native = m_media.nativeSize();
    if (native.width() > viewport.width() || native.height() > viewport.height())
        m_canvas->fitMedia();
    if (isVideo()) { scheduleVideoLoad(); scheduleContactSheetLoad(); }
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

void EditorWindow::closeEvent(QCloseEvent *e) {
    if (m_videoSaveInProgress) {
        m_closeAfterVideoSave = true;
        if (m_toast) m_toast->showMessage(QStringLiteral("Finishing video save…"));
        e->ignore();
        return;
    }
    if (!m_videoSavePendingPath.isEmpty()) {
        m_videoSavePendingClose = true;
        if (m_toast) m_toast->showMessage(QStringLiteral("Finishing video export…"));
        e->ignore();
        return;
    }
    if (m_videoIpcInProgress > 0) {
        m_closeAfterVideoIpc = true;
        if (m_toast) m_toast->showMessage(QStringLiteral("Finishing video handoff…"));
        e->ignore();
        return;
    }
    if (m_videoExportInProgress || m_videoStatusRequested) {
        m_closeAfterVideoExport = true;
        if (m_toast) m_toast->showMessage(QStringLiteral("Finishing video export…"));
        e->ignore();
        return;
    }
    QWidget::closeEvent(e);
}

void EditorWindow::updateCompactMode() {
    m_toolbar->setCompact(width() < 760);
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
    auto *lay = new QVBoxLayout(bar);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(5);
    auto *playback = new QHBoxLayout;
    playback->setSpacing(8);
    auto *trim = new QHBoxLayout;
    trim->setSpacing(8);

    m_playButton = new QToolButton(bar);
    const QColor iconColor = palette().color(QPalette::ButtonText);
    m_playButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/play.svg"), iconColor, iconColor));
    m_playButton->setObjectName("PlaybackPlay");
    m_playButton->setAutoRaise(true);
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setFixedSize(28, 28);
    m_playButton->setToolTip(QStringLiteral("Play / Pause"));
    m_playButton->setAccessibleName(m_playButton->toolTip());
    m_timeLabel = new QLabel(QStringLiteral("0:00 / ") + formatTime(m_media.video.durationMs), bar);
    m_timeLabel->setObjectName("PlaybackTime");

    m_muteButton = new QToolButton(bar);
    m_muteButton->setObjectName("PlaybackMute");
    m_muteButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/volume.svg"), iconColor, iconColor));
    m_muteButton->setAutoRaise(true);
    m_muteButton->setFocusPolicy(Qt::NoFocus);
    m_muteButton->setCursor(Qt::PointingHandCursor);
    m_muteButton->setFixedSize(28, 28);
    m_muteButton->setToolTip(QStringLiteral("Mute audio"));
    m_muteButton->setAccessibleName(m_muteButton->toolTip());

    m_volumeSlider = new QSlider(Qt::Horizontal, bar);
    m_volumeSlider->setObjectName("PlaybackVolume");
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(72);
    m_volumeSlider->setToolTip(QStringLiteral("Volume"));
    m_volumeSlider->setAccessibleName(QStringLiteral("Volume"));

    playback->addWidget(m_playButton);
    playback->addWidget(m_timeLabel);
    playback->addStretch(1);
    playback->addWidget(m_muteButton);
    playback->addWidget(m_volumeSlider);

    auto *trimLabel = new QLabel(QStringLiteral("Trim"), bar);
    trimLabel->setObjectName(QStringLiteral("TrimLabel"));
    m_timeline = new VideoTimeline(bar);
    m_timeline->setDuration(m_media.video.durationMs);
    m_timeline->setMinimumRange(m_media.video.fps > 0.0
        ? qMax<qint64>(1, qRound64(1000.0 / m_media.video.fps)) : 1);
    m_timeline->setTrimRange(m_trimInMs, m_trimOutMs);
    m_trimInLabel = new QLabel(formatPreciseTime(m_trimInMs), bar);
    m_trimInLabel->setObjectName(QStringLiteral("TrimInTime"));
    m_trimOutLabel = new QLabel(formatPreciseTime(m_trimOutMs), bar);
    m_trimOutLabel->setObjectName(QStringLiteral("TrimOutTime"));
    auto makeTrimButton = [bar](const QString &text, const QString &name) {
        auto *button = new QToolButton(bar);
        button->setText(text);
        button->setObjectName(name);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(28);
        return button;
    };
    auto *setIn = makeTrimButton(QStringLiteral("Set In"), QStringLiteral("TrimSetIn"));
    auto *setOut = makeTrimButton(QStringLiteral("Set Out"), QStringLiteral("TrimSetOut"));
    auto *reset = makeTrimButton({}, QStringLiteral("TrimReset"));
    reset->setIcon(theme::tintedIcon(QStringLiteral(":/icons/reset.svg"), iconColor, iconColor));
    reset->setFixedWidth(28);
    setIn->setToolTip(QStringLiteral("Set trim start · I"));
    setOut->setToolTip(QStringLiteral("Set trim end · O"));
    reset->setToolTip(QStringLiteral("Use the complete clip"));
    reset->setAccessibleName(reset->toolTip());

    trim->addWidget(trimLabel);
    trim->addWidget(m_trimInLabel);
    trim->addWidget(m_timeline, 1);
    trim->addWidget(m_trimOutLabel);
    trim->addWidget(setIn);
    trim->addWidget(setOut);
    trim->addWidget(reset);
    lay->addLayout(playback);
    lay->addLayout(trim);

    connect(m_playButton, &QToolButton::clicked, this, [this]{
        ensureVideoPlayer();
        if (!m_player) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState) {
            m_player->pause();
        } else {
            if (m_player->position() < m_trimInMs || m_player->position() >= m_trimOutMs)
                m_player->setPosition(m_trimInMs);
            m_player->play();
        }
    });
    connect(m_muteButton, &QToolButton::clicked, this, [this]{
        ensureVideoPlayer();
        if (!m_audioOutput) return;
        const bool muted = !m_audioOutput->isMuted();
        m_audioOutput->setMuted(muted);
        m_muteButton->setIcon(theme::tintedIcon(
            muted ? QStringLiteral(":/icons/muted.svg") : QStringLiteral(":/icons/volume.svg"),
            palette().color(QPalette::ButtonText), palette().color(QPalette::ButtonText)));
        m_muteButton->setToolTip(muted ? QStringLiteral("Unmute audio") : QStringLiteral("Mute audio"));
        m_muteButton->setAccessibleName(m_muteButton->toolTip());
    });
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value){
        ensureVideoPlayer();
        if (!m_audioOutput) return;
        m_audioOutput->setVolume(value / 100.0f);
        if (value > 0 && m_audioOutput->isMuted()) {
            m_audioOutput->setMuted(false);
            const QColor color = palette().color(QPalette::ButtonText);
            m_muteButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/volume.svg"), color, color));
            m_muteButton->setToolTip(QStringLiteral("Mute audio"));
            m_muteButton->setAccessibleName(m_muteButton->toolTip());
        }
    });
    connect(m_timeline, &VideoTimeline::seekRequested, this, [this](qint64 position){
        ensureVideoPlayer();
        if (m_player) m_player->setPosition(position);
        m_timeline->setPosition(position);
    });
    connect(m_timeline, &VideoTimeline::trimCommitted,
            this, &EditorWindow::applyTrimRange);
    connect(m_timeline, &VideoTimeline::trimPreviewed,
            this, &EditorWindow::updateTrimTimeLabels);
    connect(setIn, &QToolButton::clicked, this, [this]{
        m_timeline->setTrimRange(m_timeline->position(), m_timeline->trimOut());
        applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
    });
    connect(setOut, &QToolButton::clicked, this, [this]{
        m_timeline->setTrimRange(m_timeline->trimIn(), m_timeline->position());
        applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
    });
    connect(reset, &QToolButton::clicked, this, [this]{
        m_timeline->setTrimRange(0, m_timeline->duration());
        applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
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

void EditorWindow::scheduleContactSheetLoad() {
    if (!isVideo() || m_contactSheetQueued || !m_timeline || m_timeline->hasContactSheet()) return;
    m_contactSheetQueued = true;
    const QString path = m_media.path;
    const qint64 duration = m_media.video.durationMs;
    QPointer<EditorWindow> receiver(this);
    auto *thread = QThread::create([receiver, path, duration] {
        const ContactSheetResult result = generateVideoContactSheet(path, duration);
        QMetaObject::invokeMethod(qApp, [receiver, result] {
            if (!receiver) return;
            receiver->m_contactSheetQueued = false;
            if (result.ok && receiver->m_timeline)
                receiver->m_timeline->setContactSheet(result.image, 8);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(m_volumeSlider ? m_volumeSlider->value() / 100.0f : 1.0f);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoItem);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state){
        if (m_playButton) {
            const QColor color = palette().color(QPalette::ButtonText);
            m_playButton->setIcon(theme::tintedIcon(
                state == QMediaPlayer::PlayingState
                    ? QStringLiteral(":/icons/pause.svg") : QStringLiteral(":/icons/play.svg"),
                color, color));
        }
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration){
        if (!m_timeline) return;
        const bool usedFullRange = m_trimInMs == 0 && m_trimOutMs == m_timeline->duration();
        m_timeline->setDuration(duration);
        if (usedFullRange) {
            m_timeline->setTrimRange(0, duration);
            m_trimOutMs = duration;
        }
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(m_player->position()) + QStringLiteral(" / ") + formatTime(duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos){
        if (m_timeline) m_timeline->setPosition(pos);
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") + formatTime(m_player->duration()));
        if (m_player->playbackState() == QMediaPlayer::PlayingState && pos >= m_trimOutMs) {
            m_player->pause();
            m_player->setPosition(m_trimOutMs);
        }
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

TextItem *EditorWindow::selectedText() const {
    const auto selected = m_scene->selectedItems();
    return selected.size() == 1 ? dynamic_cast<TextItem *>(selected.first()) : nullptr;
}

void EditorWindow::refreshTextBar() {
    TextItem *text = selectedText();
    if (!text) { m_textBar->hide(); return; }
    m_textBar->setState(text->state());
    m_textBar->adjustSize();
    m_textBar->show();
    m_textBar->raise();
    positionTextBar();
}

void EditorWindow::positionTextBar() {
    if (m_textBar->isHidden()) return;
    TextItem *text = selectedText();
    if (!text) { m_textBar->hide(); return; }
    const QRectF bounds = text->sceneBoundingRect();
    const QRect item(m_canvas->mapFromScene(bounds.topLeft()),
                     m_canvas->mapFromScene(bounds.bottomRight()));
    m_textBar->move(contextBarPosition(item.normalized(), m_textBar->size(),
                                       m_canvas->viewport()->size()));
}

void EditorWindow::updateSelectedText(const std::function<void(TextItem *)> &change) {
    TextItem *text = selectedText();
    if (!text) return;
    const TextState before = text->state();
    change(text);
    const TextState after = text->state();
    if (!(after == before) && m_tools->editingText() != text)
        m_undo->push(new EditTextCommand(text, before, after));
    refreshTextBar();
}

SpotlightItem *EditorWindow::selectedSpotlight() const {
    const auto selected = m_scene->selectedItems();
    return selected.size() == 1 ? dynamic_cast<SpotlightItem *>(selected.first()) : nullptr;
}

void EditorWindow::refreshSpotlightBar() {
    SpotlightItem *spotlight = selectedSpotlight();
    if (!spotlight) { m_spotlightBar->hide(); return; }
    m_spotlightBar->setValues(spotlight->spotlightShape(), spotlight->intensity());
    m_spotlightBar->adjustSize();
    m_spotlightBar->show();
    m_spotlightBar->raise();
    positionSpotlightBar();
}

void EditorWindow::positionSpotlightBar() {
    if (m_spotlightBar->isHidden()) return;
    SpotlightItem *spotlight = selectedSpotlight();
    if (!spotlight) { m_spotlightBar->hide(); return; }
    const QRectF bounds = spotlight->mapRectToScene(spotlight->rect());
    const QRect item(m_canvas->mapFromScene(bounds.topLeft()),
                     m_canvas->mapFromScene(bounds.bottomRight()));
    m_spotlightBar->move(contextBarPosition(item.normalized(), m_spotlightBar->size(),
                                            m_canvas->viewport()->size()));
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
    const QRectF bounds = r->mapRectToScene(r->rect());
    const QRect item(m_canvas->mapFromScene(bounds.topLeft()),
                     m_canvas->mapFromScene(bounds.bottomRight()));
    m_redactBar->move(contextBarPosition(item.normalized(), m_redactBar->size(),
                                         m_canvas->viewport()->size()));
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

void EditorWindow::toggleTheme() {
    m_dark = !m_dark;
    QApplication::setPalette(theme::palette(m_dark));
    qApp->setStyleSheet(theme::styleSheet(m_dark));
    m_canvas->setBackgroundBrush(QApplication::palette().color(QPalette::Window));
    m_toolbar->setDark(m_dark);
    if (m_playButton) {
        const QColor color = palette().color(QPalette::ButtonText);
        const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
        m_playButton->setIcon(theme::tintedIcon(
            playing ? QStringLiteral(":/icons/pause.svg") : QStringLiteral(":/icons/play.svg"),
            color, color));
        const bool muted = m_audioOutput && m_audioOutput->isMuted();
        m_muteButton->setIcon(theme::tintedIcon(
            muted ? QStringLiteral(":/icons/muted.svg") : QStringLiteral(":/icons/volume.svg"),
            color, color));
        if (auto *reset = findChild<QToolButton *>(QStringLiteral("TrimReset")))
            reset->setIcon(theme::tintedIcon(QStringLiteral(":/icons/reset.svg"), color, color));
    }
    m_textBar->refreshTheme();
    m_dragPill->refreshTheme();
    m_scene->update();
#ifdef Q_OS_WIN
    applyWindowsTitleBarTheme(this, m_dark);
#endif

    QSettings settings(m_cli.configPath.isEmpty() ? defaultConfigPath() : m_cli.configPath,
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("eddy"));
    settings.setValue(QStringLiteral("theme"), m_dark ? QStringLiteral("dark")
                                                       : QStringLiteral("light"));
    settings.endGroup();
    m_cfg.theme = m_dark ? ThemeMode::Dark : ThemeMode::Light;
}

QImage EditorWindow::exportComposite() {
    if (isVideo())
        return renderAnnotationOverlay();
    const auto selection = m_scene->selectedItems();
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the image
    QImage image = renderToImage(*m_scene, m_bg.size());
    for (QGraphicsItem *item : selection) item->setSelected(true);
    return image;
}

QImage EditorWindow::renderAnnotationOverlay() {
    const auto selection = m_scene->selectedItems();
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the video
    const bool hadBackground = m_backgroundItem != nullptr;
    const bool wasVisible = hadBackground && m_backgroundItem->isVisible();
    m_renderingVideoOverlay = true;
    const int renderGeneration = ++m_videoOverlayRenderGeneration;
    if (hadBackground) m_backgroundItem->setVisible(false);
    QImage overlay = renderToImage(*m_scene, m_media.nativeSize());
    if (hadBackground) m_backgroundItem->setVisible(wasVisible);
    for (QGraphicsItem *item : selection) item->setSelected(true);
    QTimer::singleShot(50, this, [this, renderGeneration]{
        if (renderGeneration == m_videoOverlayRenderGeneration)
            m_renderingVideoOverlay = false;
    });
    return overlay;
}

bool EditorWindow::hasVideoAnnotations() const {
    if (!isVideo() || !m_scene) return false;
    for (QGraphicsItem *item : m_scene->items())
        if (item != m_backgroundItem && item->zValue() > -1000)
            return true;
    return false;
}

bool EditorWindow::hasTrim() const {
    return isVideo() && m_trimOutMs > 0
        && (m_trimInMs > 0 || m_trimOutMs < m_media.video.durationMs);
}

bool EditorWindow::hasVideoEdits() const {
    return hasVideoAnnotations() || hasTrim();
}

void EditorWindow::applyTrimRange(qint64 inMs, qint64 outMs) {
    if (!isVideo() || !m_timeline) return;
    m_timeline->setTrimRange(inMs, outMs);
    inMs = m_timeline->trimIn();
    outMs = m_timeline->trimOut();
    if (inMs == m_trimInMs && outMs == m_trimOutMs) return;
    m_undo->push(new SetTrimRangeCommand(
        m_trimInMs, m_trimOutMs, inMs, outMs,
        [this](qint64 nextIn, qint64 nextOut) { setTrimRangeState(nextIn, nextOut); }));
}

void EditorWindow::setTrimRangeState(qint64 inMs, qint64 outMs) {
    if (!m_timeline) return;
    m_timeline->setTrimRange(inMs, outMs);
    m_trimInMs = m_timeline->trimIn();
    m_trimOutMs = m_timeline->trimOut();
    updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
    if (m_player && (m_player->position() < m_trimInMs || m_player->position() > m_trimOutMs))
        m_player->setPosition(m_trimInMs);
}

void EditorWindow::updateTrimTimeLabels(qint64 inMs, qint64 outMs) {
    if (m_trimInLabel) m_trimInLabel->setText(formatPreciseTime(inMs));
    if (m_trimOutLabel) m_trimOutLabel->setText(formatPreciseTime(outMs));
}

QString EditorWindow::videoDeliveryPath() {
    if (!hasVideoEdits())
        return m_media.path;
    if (m_cachedVideoRevision == m_videoRevision && QFileInfo::exists(m_cachedVideoPath))
        return m_cachedVideoPath;
    scheduleVideoExportCache(0);
    m_videoStatusRequested = true;
    if (m_toast)
        m_toast->showMessage(QStringLiteral("Preparing video export…"));
    return {};
}

void EditorWindow::onVideoContentChanged() {
    if (!isVideo()) return;
    ++m_videoRevision;
    const bool edited = hasVideoEdits();
    if (m_dragPill) m_dragPill->setEnabled(!edited);
    if (edited) {
        scheduleVideoExportCache();
    } else if (m_videoStatusRequested) {
        completePendingVideoActions(m_media.path, false);
    }
}

void EditorWindow::scheduleVideoExportCache(int delayMs) {
    if (!isVideo() || !hasVideoEdits() || !m_videoExportTimer) return;
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
    if (!isVideo() || !hasVideoEdits()) return;
    if (m_videoExportInProgress) {
        m_videoExportPending = true;
        return;
    }

    const QString path = createVideoTempPath();
    if (path.isEmpty()) {
        if (m_videoStatusRequested) failPendingVideoActions();
        return;
    }

    const int revision = m_videoRevision;
    const VideoExportRequest request{
        m_media.path, path, renderAnnotationOverlay(),
        m_trimInMs, hasTrim() ? m_trimOutMs : -1
    };
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
    const bool current = result.ok && hasVideoEdits() && revision == m_videoRevision;
    if (current) {
        if (!m_cachedVideoPath.isEmpty() && m_cachedVideoPath != path
            && !m_clipboardVideoPaths.contains(m_cachedVideoPath)
            && !m_videoIpcPaths.contains(m_cachedVideoPath)) {
            QFile::remove(m_cachedVideoPath);
        }
        m_cachedVideoPath = path;
        m_cachedVideoRevision = revision;
        if (m_dragPill) m_dragPill->setEnabled(true);
    } else {
        QFile::remove(path);
        if (!result.ok && revision == m_videoRevision)
            std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
    }

    if (revision == m_videoRevision && m_videoStatusRequested) {
        if (result.ok)
            completePendingVideoActions(m_cachedVideoPath, true);
        else
            failPendingVideoActions();
    }

    const bool needsFreshExport = hasVideoEdits() && revision != m_videoRevision;
    if (m_videoExportPending || needsFreshExport) {
        m_videoExportPending = false;
        scheduleVideoExportCache(100);
    } else if (m_closeAfterVideoExport) {
        m_closeAfterVideoExport = false;
        close();
    }
}

void EditorWindow::copyVideoFile(const QString &path) {
    if (path.isEmpty()) return;
    for (const QString &oldPath : std::as_const(m_clipboardVideoPaths)) {
        if (oldPath != path && oldPath != m_cachedVideoPath
            && !m_videoIpcPaths.contains(oldPath)) {
            QFile::remove(oldPath);
        }
    }
    m_clipboardVideoPaths.clear();
    // ponytail: clipboard URLs need the file after Eddy exits; OS temp cleanup owns expiry.
    if (path == m_cachedVideoPath || m_videoIpcPaths.contains(path))
        m_clipboardVideoPaths.insert(path);
    QApplication::clipboard()->setMimeData(makeUrlDropMime(path));
}

void EditorWindow::runVideoIpc(
    const std::function<DeliverResult()> &operation,
    const std::function<void(const DeliverResult &)> &completion,
    const QString &pinnedPath) {
    ++m_videoIpcInProgress;
    if (!pinnedPath.isEmpty()) ++m_videoIpcPaths[pinnedPath];
    QPointer<EditorWindow> receiver(this);
    auto *thread = QThread::create([receiver, operation, completion, pinnedPath] {
        const DeliverResult result = operation();
        QMetaObject::invokeMethod(qApp, [receiver, result, completion, pinnedPath] {
            if (!receiver) return;
            --receiver->m_videoIpcInProgress;
            completion(result);
            if (!receiver) return;
            if (!pinnedPath.isEmpty()) {
                auto pin = receiver->m_videoIpcPaths.find(pinnedPath);
                if (pin != receiver->m_videoIpcPaths.end() && --pin.value() == 0)
                    receiver->m_videoIpcPaths.erase(pin);
            }
            if (!pinnedPath.isEmpty() && !receiver->m_videoIpcPaths.contains(pinnedPath)
                && pinnedPath != receiver->m_cachedVideoPath
                && !receiver->m_clipboardVideoPaths.contains(pinnedPath)) {
                QFile::remove(pinnedPath);
            }
            if (receiver && receiver->m_videoIpcInProgress == 0
                && receiver->m_closeAfterVideoIpc) {
                receiver->m_closeAfterVideoIpc = false;
                receiver->close();
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void EditorWindow::replaceVideoCard(const QString &path, bool copyAfter) {
    const bool closeAfter = m_closeAfterVideoCard;
    m_closeAfterVideoCard = false;
    const quint64 cardId = m_cli.boltsnapCardId;
    const bool shouldCopy = m_cfg.copyOnSave || copyAfter;
    const QString pinnedPath = path == m_cachedVideoPath ? path : QString();
    runVideoIpc(
        [cardId, path] { return sendVideoToBoltsnapCard(cardId, path); },
        [this, path, shouldCopy, closeAfter](const DeliverResult &result) {
            if (!result.ok) {
                std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
                copyVideoFile(path);
                if (m_toast)
                    m_toast->showMessage(QStringLiteral("Boltsnap shelf unavailable"));
                return;
            }
            if (shouldCopy) copyVideoFile(result.path);
            if (m_toast) m_toast->showMessage(QStringLiteral("Video saved"));
            if (closeAfter) close();
        }, pinnedPath);
}

void EditorWindow::startVideoFileSave(const QString &source, const QString &destination,
                                      bool copyAfter, bool closeAfter) {
    if (m_videoSaveInProgress) return;
    m_videoSaveInProgress = true;
    m_videoSaveSourcePath = source;
    m_copyAfterVideoSave = copyAfter;
    m_closeAfterVideoSave = closeAfter;
    if (m_toast) m_toast->showMessage(QStringLiteral("Saving video…"));

    QPointer<EditorWindow> receiver(this);
    auto *thread = QThread::create([receiver, source, destination] {
        const DeliverResult result = copyVideoAtomically(source, destination);
        QMetaObject::invokeMethod(qApp, [receiver, destination, result] {
            if (receiver) receiver->finishVideoFileSave(destination, result);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void EditorWindow::finishVideoFileSave(const QString &path, const DeliverResult &result) {
    m_videoSaveInProgress = false;
    const bool copyAfter = m_copyAfterVideoSave;
    const bool closeAfter = m_closeAfterVideoSave;
    const QString sourcePath = m_videoSaveSourcePath;
    m_videoSaveSourcePath.clear();
    m_copyAfterVideoSave = false;
    m_closeAfterVideoSave = false;
    if (!result.ok) {
        std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
        if (m_toast) m_toast->showMessage(QStringLiteral("Video save failed"));
        return;
    }
    const auto finish = [this, path, copyAfter, closeAfter] {
        if (copyAfter) copyVideoFile(path);
        if (m_toast) m_toast->showMessage(QStringLiteral("Video saved"));
        if (closeAfter) close();
    };
    if (m_cli.boltsnapCardId && sourcePath != m_media.path) {
        const quint64 cardId = m_cli.boltsnapCardId;
        runVideoIpc(
            [cardId, path] { return sendVideoToBoltsnapCard(cardId, path, false); },
            [finish](const DeliverResult &replaced) {
                if (!replaced.ok)
                    std::fprintf(stderr, "eddy: %s\n", qPrintable(replaced.error));
                finish();
            });
        return;
    }
    finish();
}

void EditorWindow::failPendingVideoActions() {
    m_videoStatusRequested = false;
    m_copyVideoPending = false;
    m_sendVideoToShelfPending = false;
    m_replaceVideoCardPending = false;
    m_videoSavePendingPath.clear();
    m_videoSavePendingCopy = false;
    m_videoSavePendingClose = false;
    m_closeAfterVideoShelf = false;
    m_closeAfterVideoCard = false;
    if (m_toast) m_toast->showMessage(QStringLiteral("Video export failed"));
}

void EditorWindow::completePendingVideoActions(const QString &path, bool takeOwnership) {
    const bool requested = m_videoStatusRequested;
    m_videoStatusRequested = false;

    bool copyPending = m_copyVideoPending;
    m_copyVideoPending = false;
    const bool shelfPending = m_sendVideoToShelfPending;
    m_sendVideoToShelfPending = false;
    const bool cardPending = m_replaceVideoCardPending;
    m_replaceVideoCardPending = false;
    const QString savePath = m_videoSavePendingPath;
    const bool saveCopy = m_videoSavePendingCopy;
    const bool saveClose = m_videoSavePendingClose;
    m_videoSavePendingPath.clear();
    m_videoSavePendingCopy = false;
    m_videoSavePendingClose = false;

    if (!savePath.isEmpty())
        startVideoFileSave(path, savePath, saveCopy, saveClose);
    if (cardPending) {
        replaceVideoCard(path, copyPending);
        copyPending = false;
    }
    if (shelfPending) {
        postVideoToShelf(path, takeOwnership, copyPending);
        copyPending = false;
    }
    if (copyPending) copyVideoFile(path);
    if (requested && !copyPending && savePath.isEmpty() && !cardPending && !shelfPending
        && m_toast)
        m_toast->showMessage(QStringLiteral("Video ready"));
}

void EditorWindow::saveVideo() {
    const SaveRoute route = saveRoute(m_cli, m_cfg);
    if (route == SaveRoute::Shelf) {
        m_closeAfterVideoShelf = m_cfg.earlyExit;
        m_copyVideoPending = m_copyVideoPending || m_cfg.copyOnSave;
        sendToShelf();
        return;
    }
    if (route == SaveRoute::BoltsnapCard) {
        m_closeAfterVideoCard = m_cfg.earlyExit;
        if (!hasVideoEdits()) {
            replaceVideoCard(m_media.path);
        } else if (m_cachedVideoRevision == m_videoRevision
                   && QFileInfo::exists(m_cachedVideoPath)) {
            replaceVideoCard(m_cachedVideoPath);
        } else {
            m_replaceVideoCardPending = true;
            m_videoStatusRequested = true;
            if (m_toast) m_toast->showMessage(QStringLiteral("Preparing video export…"));
            scheduleVideoExportCache(0);
        }
        return;
    }

    QString path;
    if (m_cli.output.toFile) {
        path = m_cli.output.filePath;
    } else if (m_cli.output.toStdout) {
        std::fprintf(stderr, "eddy: video export to stdout is not supported\n");
    } else if (route == SaveRoute::ExplicitOutput && !m_cli.output.saveDir.isEmpty()) {
        const QString suffix = QFileInfo(m_media.path).suffix().isEmpty()
            ? QStringLiteral("mp4")
            : QFileInfo(m_media.path).suffix();
        path = QDir(m_cli.output.saveDir).filePath(
            QStringLiteral("eddy-")
            + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss")
            + QStringLiteral(".") + suffix);
    } else if (route == SaveRoute::ConfigDirectory) {
        const QString suffix = QFileInfo(m_media.path).suffix().isEmpty()
            ? QStringLiteral("mp4")
            : QFileInfo(m_media.path).suffix();
        path = QDir(m_cfg.saveDir).filePath(
            QStringLiteral("eddy-")
            + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss")
            + QStringLiteral(".") + suffix);
    }

    if (path.isEmpty()) {
        copy();
        if (m_cfg.earlyExit && !hasVideoEdits()) close();
        return;
    }

    if (!hasVideoEdits()) {
        startVideoFileSave(m_media.path, path, m_cfg.copyOnSave, m_cfg.earlyExit);
    } else if (m_cachedVideoRevision == m_videoRevision
               && QFileInfo::exists(m_cachedVideoPath)) {
        startVideoFileSave(m_cachedVideoPath, path, m_cfg.copyOnSave, m_cfg.earlyExit);
    } else {
        m_videoSavePendingPath = path;
        m_videoSavePendingCopy = m_cfg.copyOnSave;
        m_videoSavePendingClose = m_cfg.earlyExit;
        m_videoStatusRequested = true;
        if (m_toast) m_toast->showMessage(QStringLiteral("Preparing video export…"));
        scheduleVideoExportCache(0);
    }
}

bool EditorWindow::postImageToShelf(const QImage &img, bool showSuccessToast) {
    const QString output = screen() ? screen()->name() : QString();
    const DeliverResult res = sendPngToBoltsnapShelf(
        encodePng(img), QStringLiteral("eddy"), output);
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

void EditorWindow::postVideoToShelf(const QString &path, bool takeOwnership, bool copyAfter) {
    const QString output = screen() ? screen()->name() : QString();
    const bool closeAfter = m_closeAfterVideoShelf;
    const QString pinnedPath = path == m_cachedVideoPath ? path : QString();
    m_closeAfterVideoShelf = false;
    runVideoIpc(
        [path, takeOwnership, output] {
            return sendVideoToBoltsnapShelf(
                path, QStringLiteral("eddy"), takeOwnership, output);
        },
        [this, path, copyAfter, closeAfter](const DeliverResult &result) {
            if (!result.ok) {
                std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
                if (m_toast)
                    m_toast->showMessage(QStringLiteral("Boltsnap shelf unavailable"));
                if (copyAfter) copyVideoFile(path);
                return;
            }
            if (copyAfter) copyVideoFile(result.path);
            if (m_toast) m_toast->showMessage(QStringLiteral("Sent to Boltsnap shelf"));
            if (closeAfter) close();
        }, pinnedPath);
}

void EditorWindow::save() {
    if (isVideo()) { saveVideo(); return; }
    QImage img = exportComposite();
    const SaveRoute route = saveRoute(m_cli, m_cfg);
    if (route == SaveRoute::Shelf) {
        const bool sent = postImageToShelf(img, true);
        if (m_cfg.copyOnSave || !sent)
            QApplication::clipboard()->setImage(img);
        if (m_cfg.earlyExit) close();
        return;
    }
    if (route == SaveRoute::BoltsnapCard) {
        const DeliverResult replaced = sendPngToBoltsnapCard(
            m_cli.boltsnapCardId, encodePng(img));
        if (!replaced.ok) {
            std::fprintf(stderr, "eddy: %s\n", qPrintable(replaced.error));
            if (m_toast)
                m_toast->showMessage(QStringLiteral("Boltsnap shelf unavailable"));
        }
        if (m_cfg.copyOnSave || !replaced.ok)
            QApplication::clipboard()->setImage(img);
        if (m_cfg.earlyExit) close();
        return;
    }

    // Explicit CLI output wins; a configured save_dir is the file fallback
    // after card replacement and before shelf return.
    QString path;
    if (m_cli.output.toFile)            path = m_cli.output.filePath;
    else if (m_cli.output.toStdout)     path = QStringLiteral("-");
    else if (!m_cli.output.saveDir.isEmpty())
        path = QDir(m_cli.output.saveDir).filePath(
                   "eddy-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png");
    else if (route == SaveRoute::ConfigDirectory)
        path = QDir(m_cfg.saveDir).filePath(
                   "eddy-" + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".png");

    if (!path.isEmpty()) {
        auto res = writePng(img, path);
        if (!res.ok) std::fprintf(stderr, "eddy: %s\n", qPrintable(res.error));
        else if (m_cli.boltsnapCardId) {
            const DeliverResult replaced = sendPngToBoltsnapCard(
                m_cli.boltsnapCardId, encodePng(img));
            if (!replaced.ok)
                std::fprintf(stderr, "eddy: %s\n", qPrintable(replaced.error));
        }
    }
    if (m_cfg.copyOnSave || path.isEmpty())
        QApplication::clipboard()->setImage(img);   // always at least copy
    if (m_cfg.earlyExit) close();
}

void EditorWindow::sendToShelf() {
    if (isVideo()) {
        if (!hasVideoEdits()) {
            const bool copyAfter = m_copyVideoPending;
            m_copyVideoPending = false;
            postVideoToShelf(m_media.path, false, copyAfter);
            return;
        }
        if (m_cachedVideoRevision == m_videoRevision && QFileInfo::exists(m_cachedVideoPath)) {
            const bool copyAfter = m_copyVideoPending;
            m_copyVideoPending = false;
            postVideoToShelf(m_cachedVideoPath, true, copyAfter);
            return;
        }
        m_sendVideoToShelfPending = true;
        m_videoStatusRequested = true;
        if (m_toast) m_toast->showMessage(QStringLiteral("Preparing video export…"));
        scheduleVideoExportCache(0);
        return;
    }
    postImageToShelf(exportComposite(), true);
}

void EditorWindow::copy() {
    if (isVideo()) {
        const QString path = videoDeliveryPath();
        if (path.isEmpty())
            m_copyVideoPending = true;
        else
            copyVideoFile(path);
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
    // hotkeys — let them type into the text box. Esc reverts; Ctrl+Enter commits.
    QGraphicsItem *fi = m_scene->focusItem();
    if (fi && fi->type() == TextItem::Type
        && (static_cast<QGraphicsTextItem *>(fi)->textInteractionFlags() & Qt::TextEditorInteraction)) {
        if (e->key() == Qt::Key_Escape) { m_tools->cancelTextEdit(); e->accept(); return; }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
            && e->modifiers().testFlag(Qt::ControlModifier)) {
            m_tools->commitTextEdit(); e->accept(); return;
        }
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
        case Qt::Key_I:
            if (isVideo() && m_timeline) {
                m_timeline->setTrimRange(m_timeline->position(), m_timeline->trimOut());
                applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
                break;
            }
            QWidget::keyPressEvent(e);
            break;
        case Qt::Key_O:
            if (isVideo() && m_timeline) {
                m_timeline->setTrimRange(m_timeline->trimIn(), m_timeline->position());
                applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
                break;
            }
            QWidget::keyPressEvent(e);
            break;
        case Qt::Key_J:
        case Qt::Key_L:
            if (isVideo() && m_timeline) {
                const qint64 frame = m_media.video.fps > 0.0
                    ? qMax<qint64>(1, qRound64(1000.0 / m_media.video.fps)) : 33;
                const qint64 delta = e->key() == Qt::Key_J ? -frame : frame;
                const qint64 position = qBound<qint64>(0, m_timeline->position() + delta,
                                                        m_timeline->duration());
                m_timeline->setPosition(position);
                ensureVideoPlayer();
                if (m_player) m_player->setPosition(position);
                break;
            }
            QWidget::keyPressEvent(e);
            break;
        case Qt::Key_K:
            if (isVideo() && m_playButton) {
                m_playButton->click();
                break;
            }
            QWidget::keyPressEvent(e);
            break;
        case Qt::Key_Space:
            m_canvas->setSpacePan(true);
            e->accept();
            return;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const qreal distance = e->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
            QPointF delta;
            if (e->key() == Qt::Key_Left) delta.setX(-distance);
            if (e->key() == Qt::Key_Right) delta.setX(distance);
            if (e->key() == Qt::Key_Up) delta.setY(-distance);
            if (e->key() == Qt::Key_Down) delta.setY(distance);
            if (!m_tools->nudgeSelection(delta)) QWidget::keyPressEvent(e);
            break;
        }
        case Qt::Key_0: m_canvas->fitMedia(); break;
        case Qt::Key_1: m_canvas->resetZoom(); break;
        case Qt::Key_Plus: case Qt::Key_Equal: m_canvas->zoomBy(1.15); break;
        case Qt::Key_Minus: m_canvas->zoomBy(1.0 / 1.15); break;
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
        case Qt::Key_D: if (e->modifiers() & Qt::ControlModifier)
                            m_tools->duplicateSelection(QPointF(8,8));
                        else QWidget::keyPressEvent(e);
                        break;
        case Qt::Key_S: if (e->modifiers() & Qt::ControlModifier) save(); break;
        case Qt::Key_Return: case Qt::Key_Enter: save(); break;
        case Qt::Key_Delete: case Qt::Key_Backspace: {
            const auto sel = m_scene->selectedItems();
            QList<QGraphicsItem *> removable;
            for (QGraphicsItem *it : sel) {
                if (it->zValue() <= -1000) continue;     // never the background
                if (auto *r = dynamic_cast<RedactItem *>(it)) m_ocr->forget(r);
                removable.append(it);
            }
            if (!removable.isEmpty()) {
                m_undo->beginMacro(QStringLiteral("Delete"));
                for (QGraphicsItem *it : removable)
                    m_undo->push(new RemoveItemCommand(m_scene, it));
                m_undo->endMacro();
            }
            else QWidget::keyPressEvent(e);
            break;
        }
        case Qt::Key_Escape:
            if (m_tools->cancelActive()) break;
            if (m_canvas->spacePanActive()) { m_canvas->setSpacePan(false); break; }
            close();
            break;
        default: QWidget::keyPressEvent(e);
    }
}

void EditorWindow::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) {
        m_canvas->setSpacePan(false);
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

}

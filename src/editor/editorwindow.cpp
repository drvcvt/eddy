#include "editor/editorwindow.h"
#include "app/fills.h"
#include <QGuiApplication>
#include "app/spring.h"
#include "bars/stepbar.h"
#include "bars/selectionbar.h"
#include "canvas/snapping.h"
#include "items/labels/stepitem.h"
#include "timeline/audiowaveform.h"
#include "canvas/canvas.h"
#include "edit/cropcontroller.h"
#include "bars/cropbar.h"
#include "chrome/toolbar.h"
#include "edit/toolcontroller.h"
#include "output/exporter.h"
#include "project/boltsnapipc.h"
#include "video/videoexporter.h"
#include "popovers/studiopopover.h"
#include <QFileDialog>
#include "timeline/videotimeline.h"
#include "video/videopreviewprovider.h"
#include "canvas/selectionhandles.h"
#include "edit/undocommands.h"
#include "items/labels/textitem.h"
#include "bars/redactbar.h"
#include "bars/textbar.h"
#include "bars/spotlightbar.h"
#include "items/regions/spotlightitem.h"
#include "chrome/toast.h"
#include "chrome/dragpill.h"
#include "edit/redactocrcontroller.h"
#include "items/regions/redactitem.h"
#include "canvas/previewitems.h"
#include "timeline/zoomlane.h"
#include "timeline/zoombar.h"
#include "timeline/fragmentbar.h"
#include "video/fragments.h"
#include "studio/zoomsuggest.h"
#include "studio/studiopresets.h"
#include <QInputDialog>
#include "popovers/exportpanel.h"
#include "project/projectstore.h"
#include "project/recoverystore.h"
#include "popovers/resumedialog.h"
#include <QThread>
#include "chrome/minimap.h"
#include "chrome/motionicon.h"
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <cstdio>
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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
#include <QHelpEvent>
#include <QLineEdit>
#include <QMenu>
#include <QActionGroup>
#include <cmath>
#include <QtMath>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QWidgetAction>
#include <QGraphicsOpacityEffect>
#include <QPainter>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

#include "editor/editorwindow_p.h"

namespace eddy {

static bool g_recoveryByDefault = false;

void EditorWindow::setRecoveryEnabledByDefault(bool on) { g_recoveryByDefault = on; }

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

} // namespace

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

// Qt 6.11's PipeWire audio backend can crash in its own thread while the first audio
// output is created (thread QAudioContext in libpipewire-module-protocol-native; caught
// once in 25 runs of test_uigates under gdb, 2026-10-27). Where a Pulse server answers,
// pipewire-pulse included, Qt's PulseAudio backend plays the same sound without it.
// A QT_AUDIO_BACKEND the user set always wins. Before the first QAudioOutput.
static void preferPulseAudio() {
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsSet("QT_AUDIO_BACKEND")) return;
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QFileInfo::exists(runtime + QStringLiteral("/pulse/native")))
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");
#endif
}

EditorWindow::EditorWindow(const QImage &image, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : EditorWindow(imageDocument(image), cfg, cli, parent) {}

EditorWindow::~EditorWindow() {
    QObject::disconnect(m_scene, nullptr, this, nullptr);
    QObject::disconnect(m_undo, nullptr, this, nullptr);
    // An open Studio popover is deleted with us; its close handler must not run
    // against a half-destroyed window.
    if (m_studioPopover) QObject::disconnect(m_studioPopover, nullptr, this, nullptr);
    if (!m_cachedVideoPath.isEmpty() && !m_clipboardVideoPaths.contains(m_cachedVideoPath)
        && !m_videoIpcPaths.contains(m_cachedVideoPath))
        QFile::remove(m_cachedVideoPath);
}

EditorWindow::EditorWindow(const MediaDocument &media, const Config &cfg, const CliOptions &cli, QWidget *parent)
    : QWidget(parent), m_media(media), m_bg(toolBackgroundFor(media)), m_cfg(cfg), m_cli(cli) {
    preferPulseAudio();
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
        auto *bgItem = new PreviewPixmapItem(QPixmap::fromImage(m_bg));
        m_scene->addItem(bgItem);
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

    auto *lay = new QGridLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(m_toolbar, 0, 0, 1, 2);
    lay->addWidget(m_toolbar->toolRail(), 1, 0, Qt::AlignTop);
    m_toolbar->toolRail()->show();
    lay->addWidget(m_canvas, 1, 1);
    lay->setRowStretch(1, 1);
    lay->setColumnStretch(1, 1);
    if (isVideo())
        lay->addWidget(createPlaybackBar(), 2, 0, 1, 2);

    connect(m_toolbar, &Toolbar::toolChosen, m_tools, &ToolController::setTool);
    connect(m_toolbar, &Toolbar::colorChosen, this, [this](const QColor &color){
        m_tools->setColor(color);
        updateSelectedText([color](TextItem *text){ text->setAnnotationColor(color); });
    });
    connect(m_toolbar, &Toolbar::saveRequested, this, &EditorWindow::save);
    connect(m_toolbar, &Toolbar::copyRequested, this, &EditorWindow::copy);
    if (isVideo()) {
        m_toolbar->enableVideoFrameCopy();
        addAction(m_toolbar->findChild<QAction *>(QStringLiteral("CopyVideoFrame")));
        connect(m_toolbar, &Toolbar::copyFrameRequested, this, &EditorWindow::copyVideoFrame);
    }
    connect(m_toolbar, &Toolbar::sendToShelfRequested, this, &EditorWindow::sendToShelf);
    connect(m_tools, &ToolController::toolChanged, m_toolbar, &Toolbar::syncTool);
    connect(m_toolbar, &Toolbar::widthChosen, m_tools, &ToolController::setWidth);
    connect(m_toolbar, &Toolbar::undoRequested, this, &EditorWindow::doUndo);
    connect(m_toolbar, &Toolbar::redoRequested, this, &EditorWindow::doRedo);
    connect(m_undo, &QUndoStack::canUndoChanged, m_toolbar, &Toolbar::setUndoEnabled);
    connect(m_undo, &QUndoStack::canRedoChanged, m_toolbar, &Toolbar::setRedoEnabled);
    connect(m_toolbar, &Toolbar::eyedropperRequested, this, [this]{
        m_canvas->startEyedropper();
        if (m_toast) m_toast->showMessage(QStringLiteral("Click to pick a colour, Esc to cancel"));
    });
    connect(m_toolbar, &Toolbar::themeToggleRequested, this, &EditorWindow::toggleTheme);
    connect(m_toolbar, &Toolbar::studioRequested, this, &EditorWindow::openStudio);
    connect(m_canvas, &Canvas::colorPicked, this, [this](const QColor &c){
        m_tools->setColor(c);
        m_toolbar->setSwatchColor(c);
    });
    m_toolbar->setSwatchColor(cfg.strokeColor);   // disc starts in the real stroke colour
    if (isVideo()) {
        m_videoExportTimer = new QTimer(this);
        m_videoExportTimer->setObjectName(QStringLiteral("VideoExportTimer"));
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
    m_stepBar = new StepBar(m_canvas->viewport());
    m_stepBar->hide();
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshStepBar);
    connect(m_undo, &QUndoStack::indexChanged, this, &EditorWindow::refreshStepBar);
    // Scene changes come with every video frame: they only move a bar that shows.
    connect(m_scene, &QGraphicsScene::changed, this, &EditorWindow::positionContextBars);
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionContextBars);
    connect(m_stepBar, &StepBar::numberChosen, this, [this](int number) {
        if (StepItem *step = selectedStep())
            m_undo->push(new SetStepCommand(step, step->number(), step->size(), number, step->size()));
    });
    connect(m_stepBar, &StepBar::sizeChosen, this, [this](StepItem::Size size) {
        m_tools->setStepSize(size);   // the next step takes it too
        if (StepItem *step = selectedStep(); step && step->size() != size)
            m_undo->push(new SetStepCommand(step, step->number(), step->size(), step->number(), size));
    });
    connect(m_stepBar, &StepBar::renumberRequested, this, &EditorWindow::renumberSteps);
    m_selectionBar = new SelectionBar(m_canvas->viewport());
    m_selectionBar->hide();
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshSelectionBar);
    connect(m_undo, &QUndoStack::indexChanged, this, &EditorWindow::refreshSelectionBar);
    connect(m_selectionBar, &SelectionBar::alignChosen, this, [this](Align align) {
        QVector<QRectF> boxes;
        for (QGraphicsItem *item : alignableSelection()) boxes.append(alignmentBounds(item));
        moveSelection(alignDeltas(boxes, align), tr("Align"));
    });
    connect(m_selectionBar, &SelectionBar::distributeChosen, this, [this](Qt::Orientation orientation) {
        QVector<QRectF> boxes;
        for (QGraphicsItem *item : alignableSelection()) boxes.append(alignmentBounds(item));
        moveSelection(distributeDeltas(boxes, orientation), tr("Space evenly"));
    });
    m_toast = new Toast(this);
    m_tooltip = new QLabel(this);
    m_tooltip->setObjectName(QStringLiteral("CompactTooltip"));
    m_tooltip->setTextFormat(Qt::RichText);
    m_tooltip->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tooltip->hide();
    m_tooltipTimer = new QTimer(this);
    m_tooltipTimer->setSingleShot(true);
    connect(m_tooltipTimer, &QTimer::timeout, m_tooltip, &QWidget::hide);
    qApp->installEventFilter(this);

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
    connect(m_redactBar, &RedactBar::timeScopeChosen, this, [this](bool fromPlayhead) {
        setTimeScope(selectedRedact(), fromPlayhead);
    });
    connect(m_spotlightBar, &SpotlightBar::timeScopeChosen, this, [this](bool fromPlayhead) {
        setTimeScope(selectedSpotlight(), fromPlayhead);
    });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorWindow::refreshMasks);
    connect(m_undo, &QUndoStack::indexChanged, this, &EditorWindow::refreshMasks);
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
    const QString name = m_media.path.isEmpty()
        ? QStringLiteral("Image") : QFileInfo(m_media.path).fileName();
    setWindowTitle(QStringLiteral("%1 (%2 × %3)")
        .arg(name).arg(native.width()).arg(native.height()));
    auto *viewControls = new QWidget(this);
    viewControls->setObjectName(QStringLiteral("ViewControls"));
    auto *fl = new QGridLayout(viewControls);
    fl->setContentsMargins(theme::kBarPad, theme::kBarPad, theme::kBarPad, theme::kBarPad);
    fl->setSpacing(theme::kS1);
    // Three columns like the playback row: zoom left, the pill in the middle, status right.
    // The sides share one width while that fits (balanceFooter), so the pill sits on the
    // window's centre; when it does not, the pill gives way instead of being covered.
    fl->setColumnStretch(0, 1);
    fl->setColumnStretch(2, 1);
    fl->setHorizontalSpacing(theme::kS2);
    auto *zoomControls = new QHBoxLayout;
    zoomControls->setSpacing(theme::kS1);
    fl->addLayout(zoomControls, 0, 0, Qt::AlignLeft);
    m_footerGrid = fl;
    m_footerLeft = zoomControls;
    auto *fit = new theme::CapButton(viewControls);
    fit->setObjectName(QStringLiteral("ZoomFit"));
    fit->setText(QStringLiteral("Fit"));
    fit->setToolTip(QStringLiteral("Fit to window\t0"));
    fit->setAccessibleName(QStringLiteral("Fit to window"));
    fit->setFocusPolicy(Qt::NoFocus);
    fit->setCursor(Qt::PointingHandCursor);
    fit->setFixedHeight(theme::kCtl);
    connect(fit, &QToolButton::clicked, m_canvas, &Canvas::fitMedia);
    zoomControls->addWidget(fit);
    auto *zoom = new theme::CapButton(viewControls);
    zoom->setObjectName(QStringLiteral("ZoomActual"));
    zoom->setToolTip(QStringLiteral("Actual size\t1"));
    zoom->setAccessibleName(QStringLiteral("Actual size"));
    zoom->setFocusPolicy(Qt::NoFocus);
    zoom->setCursor(Qt::PointingHandCursor);
    zoom->setFixedSize(52, theme::kCtl);
    connect(zoom, &QToolButton::clicked, m_canvas, &Canvas::resetZoom);
    const auto updateZoom = [this, zoom] {
        zoom->setText(QStringLiteral("%1%").arg(qRound(m_canvas->zoom() * 100)));
    };
    connect(m_canvas, &Canvas::viewChanged, zoom, updateZoom);
    updateZoom();
    zoomControls->addWidget(zoom);
    // Align independently within the full row, not the space left after zoom.
    fl->addWidget(m_dragPill, 0, 1, Qt::AlignCenter);
    if (isVideo()) {
        m_exportStatus = new QLabel(viewControls);
        m_exportStatus->setObjectName(QStringLiteral("VideoExportStatus"));
        m_exportStatus->setFixedWidth(112);   // "Cancelling…" at the caption size, so the bar never jumps
        m_exportStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_exportStatus->setToolTip(tr("Video export status"));
        m_exportCancel = new QToolButton(viewControls);
        m_exportCancel->setObjectName(QStringLiteral("VideoExportCancel"));
        m_exportCancel->setText(tr("Cancel"));
        m_exportCancel->setToolTip(tr("Stop the running video export"));
        m_exportCancel->setAccessibleName(tr("Cancel video export"));
        m_exportCancel->setFocusPolicy(Qt::NoFocus);
        m_exportCancel->setCursor(Qt::PointingHandCursor);
        m_exportCancel->setFixedHeight(theme::kCtl);
        // Its slot stays while it is hidden: the status beside it never moves.
        QSizePolicy keep = m_exportCancel->sizePolicy();
        keep.setRetainSizeWhenHidden(true);
        m_exportCancel->setSizePolicy(keep);
        m_exportCancel->hide();
        connect(m_exportCancel, &QToolButton::clicked, this, &EditorWindow::cancelVideoExport);
        auto *exportControls = new QHBoxLayout;
        exportControls->setSpacing(theme::kS1);
        exportControls->addWidget(m_exportCancel);   // its kept slot sits left: the status stays on the edge
        exportControls->addWidget(m_exportStatus);
        // On the window's edge, like the row's speaker above it; the group grows to the
        // left, so the pill (centred on its own) and the transport row stay put.
        exportControls->addWidget(m_noAudio);
        fl->addLayout(exportControls, 0, 2, Qt::AlignRight);
        m_footerRight = exportControls;
    } else {
        // An empty column drops its spacing and pulls the pill off the centre: hold it.
        auto *holder = new QWidget(viewControls);
        holder->setFixedHeight(0);
        holder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // grows like the left column
        fl->addWidget(holder, 0, 2);
    }
    viewControls->setAttribute(Qt::WA_StyledBackground, true);
    lay->addWidget(viewControls, isVideo() ? 3 : 2, 0, 1, 2);
    m_canvas->setAnimationsEnabled(cfg.animations);
    m_tools->setAnimationsEnabled(cfg.animations);
    Spring::setEnabled(cfg.animations);
    HoverFills::install();
    // Sync the toolbar to the configured default explicitly: the earlier
    // m_tools->setTool() ran before this connect existed (its toolChanged was
    // dropped), and setTool only emits on a *change* — so a default of "arrow"
    // (the controller's default) would emit nothing at all. Keep this call.
    m_toolbar->syncTool(toolFromName(cfg.defaultTool));
    setupCrop();
    m_recoveryEnabled = g_recoveryByDefault;
    m_recoveryIdle = new QTimer(this);
    m_recoveryIdle->setSingleShot(true);
    m_recoveryIdle->setInterval(2000);
    connect(m_recoveryIdle, &QTimer::timeout, this, &EditorWindow::writeRecovery);
    m_recoveryMax = new QTimer(this);
    m_recoveryMax->setSingleShot(true);
    m_recoveryMax->setInterval(10000);
    connect(m_recoveryMax, &QTimer::timeout, this, &EditorWindow::writeRecovery);
    connect(m_undo, &QUndoStack::indexChanged, this, [this] { if (m_recoveryEnabled) noteRecoveryChange(); });
    // A right-click on Save opens the export popover; its footer holds the project actions.
    m_exportPanel = new ExportPanel;
    m_exportPanel->setVideo(isVideo());
    m_exportPanel->setSound(m_studio.audio, isVideo() && m_media.video.hasAudio);
    QMenu *exportMenu = m_toolbar->enableExportMenu(m_exportPanel);
    connect(exportMenu, &QMenu::aboutToShow, this, &EditorWindow::openExportPanel);
    connect(m_exportPanel, &ExportPanel::saveRequested, this, [this, exportMenu] {
        exportMenu->close();
        save();
    });
    connect(m_exportPanel, &ExportPanel::projectSaveRequested, this, [this, exportMenu] {
        exportMenu->close();
        saveProject();
    });
    connect(m_exportPanel, &ExportPanel::projectOpenRequested, this, [this, exportMenu] {
        exportMenu->close();
        openProjectDialog();
    });
    connect(m_exportPanel, &ExportPanel::soundChosen, this, [this](bool on) {
        editStudio([on](StudioDocument &d) { d.audio = on; });
    });
    connect(m_exportPanel, &ExportPanel::resumeRequested, this, [this, exportMenu] {
        exportMenu->close();
        openResumeDialog();
    });
    if (isVideo()) {
        m_exportSettings = loadExportSettings(configPath());
        m_exportPanel->setSettings(m_exportSettings);
        connect(m_exportPanel, &ExportPanel::settingsChanged, this, [this](const ExportSettings &settings) {
            m_exportSettings = settings;
            saveExportSettings(configPath(), settings);
            onVideoContentChanged();   // the cached export no longer matches
            m_exportPanel->setKnownSize(std::nullopt);   // nor does its size
        });
        if (m_media.cursorTrack) m_cursorTrack = std::make_shared<const CursorTrack>(*m_media.cursorTrack);
        m_cameraRebuild = new QTimer(this);
        m_cameraRebuild->setSingleShot(true);
        m_cameraRebuild->setInterval(16);
        connect(m_cameraRebuild, &QTimer::timeout, this, &EditorWindow::rebuildCamera);
        m_zoomBar = new ZoomBar(m_canvas->viewport());
        m_zoomBar->hide();
        m_miniMap = new MiniMap(m_canvas->viewport());
        m_miniMap->hide();
        m_fragmentBar = new FragmentBar(m_canvas->viewport());
        m_fragmentBar->hide();
        connect(m_fragmentBar, &FragmentBar::speedChosen, this, [this](double speed) {
            const int i = m_selectedFragment;
            editStudio([i, speed](StudioDocument &d) { fragments::setSpeed(d.fragments, i, speed); });
        });
        connect(m_fragmentBar, &FragmentBar::cutToggled, this, [this] { toggleFragmentCut(m_selectedFragment); });
        connect(m_fragmentBar, &FragmentBar::joinRequested, this, [this] {
            const int i = m_selectedFragment;
            QVector<Fragment> list = m_studio.fragments;
            if (!fragments::join(list, i)) return;
            editStudio([&](StudioDocument &d) { d.fragments = list; });
            selectFragment(list.isEmpty() ? -1 : i - 1);
        });
        connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionZoomUi);
        connect(m_zoomBar, &ZoomBar::scaleChosen, this, [this](double scale) {
            editZoom(m_selectedZoom, [scale](ZoomSegment &z) { z.scale = scale; });
        });
        connect(m_zoomBar, &ZoomBar::motionChosen, this, [this](ZoomSegment::Motion motion) {
            editZoom(m_selectedZoom, [motion](ZoomSegment &z) { z.motion = motion; });
        });
        connect(m_zoomBar, &ZoomBar::removeRequested, this, [this] { removeZoom(m_selectedZoom); });
        m_zoomBar->setCursorAvailable(m_cursorTrack != nullptr);
        connect(m_zoomBar, &ZoomBar::targetChosen, this, [this](ZoomSegment::Target target) {
            editZoom(m_selectedZoom, [target](ZoomSegment &z) { z.target = target; });
        });
        connect(m_miniMap, &MiniMap::dragged, this, &EditorWindow::moveCameraTarget);
        connect(m_miniMap, &MiniMap::dragFinished, this, &EditorWindow::finishCameraGesture);
        connect(m_miniMap, &MiniMap::wheelZoom, this, [this](double steps) {
            editZoom(m_selectedZoom, [steps](ZoomSegment &z) {
                z.scale = qBound(1.1, z.scale * std::pow(1.1, steps), 4.0);
            }, int(m_selectedZoom));
        });
        // The content follows the pointer, so the camera moves the other way.
        connect(m_canvas, &Canvas::cameraDragged, this, [this](QPointF d) { moveCameraTarget(-d); });
        connect(m_canvas, &Canvas::cameraDragFinished, this, &EditorWindow::finishCameraGesture);
        rebuildCamera();
        connect(m_tools, &ToolController::toolChanged, this, [this](ToolType type) {
            if (type == ToolType::Redact) updateVideoBackground();
        });
        connect(m_dragPill, &DragPill::preparationRequested, this, [this] { videoDeliveryPath(); });
    }
    if (cfg.animations && canFadeWindow()) setWindowOpacity(0.0);   // entrance fade starts transparent

    // Size the canvas to the image at 100%; only oversized media starts fitted.
    const int maxW = 1700, maxH = 1000;
    const int chromeW = m_toolbar->toolRail()->sizeHint().width();
    const int chromeH = m_toolbar->sizeHint().height() + viewControls->sizeHint().height()
        + (isVideo() ? findChild<QWidget *>(QStringLiteral("PlaybackBar"))->sizeHint().height() : 0);
    const int minW = qMax(isVideo() ? 520 : 760, lay->minimumSize().width());
    setMinimumSize(minW, chromeH + m_toolbar->toolRail()->sizeHint().height());
    resize(qMin(qMax(native.width() + chromeW, minW), maxW), qMin(native.height() + chromeH, maxH));
}

void EditorWindow::balanceFooter() {
    if (!m_footerGrid) return;
    QWidget *footer = m_footerGrid->parentWidget();
    const int side = qMax(m_footerLeft->sizeHint().width(), m_footerRight ? m_footerRight->sizeHint().width() : 0);
    const QMargins m = m_footerGrid->contentsMargins();
    const int needed = 2 * side + m_dragPill->sizeHint().width() + 2 * m_footerGrid->horizontalSpacing()
                       + m.left() + m.right();
    const int balanced = footer->width() >= needed ? side : 0;
    if (m_footerGrid->columnMinimumWidth(0) == balanced && m_footerGrid->columnMinimumWidth(2) == balanced) return;
    m_footerGrid->setColumnMinimumWidth(0, balanced);
    m_footerGrid->setColumnMinimumWidth(2, balanced);
}

// Wayland gives a client no say over its window's opacity (Qt warns on every frame and
// does nothing); the compositor animates new windows itself.
bool EditorWindow::canFadeWindow() {
    return !QGuiApplication::platformName().startsWith(QLatin1String("wayland"));
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
    if (isVideo()) { scheduleVideoLoad(); scheduleStripLoad(); }
    QTimer::singleShot(0, this, &EditorWindow::offerKeptEdit);
    if (!m_cfg.animations || !canFadeWindow()) return;
    auto *fade = new Spring(motion::kBase, [this](qreal opacity) { setWindowOpacity(opacity); }, this);
    fade->jumpTo(0);
    fade->setTarget(1);
}

bool EditorWindow::closePending() const {
    return m_closeAfterVideoSave || m_videoSavePendingClose || m_closeAfterVideoIpc || m_closeAfterVideoExport;
}

void EditorWindow::showClosePending() {
    if (m_exportStatus) m_exportStatus->setText(tr("Closes when done"));
    if (m_toast) m_toast->showAction(tr("Closes when the export is done"), tr("Stay open"), [this] { withdrawClose(); });
}

// Only the close that was asked for: the work goes on, the window stays.
void EditorWindow::withdrawClose() {
    if (!closePending()) return;
    m_closeAfterVideoSave = m_videoSavePendingClose = m_closeAfterVideoIpc = m_closeAfterVideoExport = false;
    if (m_exportStatus) m_exportStatus->setText(QString());
    if (m_toast) m_toast->showMessage(tr("Stays open"));
}

void EditorWindow::closeEvent(QCloseEvent *e) {
    if (m_videoSaveInProgress) {
        m_closeAfterVideoSave = true;
        showClosePending();
        e->ignore();
        return;
    }
    if (!m_videoSavePendingPath.isEmpty()) {
        m_videoSavePendingClose = true;
        showClosePending();
        e->ignore();
        return;
    }
    if (m_videoIpcInProgress > 0) {
        m_closeAfterVideoIpc = true;
        showClosePending();
        e->ignore();
        return;
    }
    if (m_videoExportInProgress || m_videoStatusRequested) {
        m_closeAfterVideoExport = true;
        showClosePending();
        e->ignore();
        return;
    }
    if (m_recoveryWriting) {
        m_closeAfterRecovery = true;
        e->ignore();
        return;
    }
    flushRecovery();
    QWidget::closeEvent(e);
}

} // namespace eddy

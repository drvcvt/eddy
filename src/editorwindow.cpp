#include "editorwindow.h"
#include "canvas.h"
#include "cropcontroller.h"
#include "cropbar.h"
#include "toolbar.h"
#include "toolcontroller.h"
#include "exporter.h"
#include "boltsnapipc.h"
#include "videoexporter.h"
#include "studiopopover.h"
#include <QFileDialog>
#include "videotimeline.h"
#include "videopreviewprovider.h"
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
#include "previewitems.h"
#include "zoomlane.h"
#include "zoombar.h"
#include "fragmentbar.h"
#include "fragments.h"
#include "zoomsuggest.h"
#include "exportpanel.h"
#include "projectstore.h"
#include "recoverystore.h"
#include "resumedialog.h"
#include <QThread>
#include "minimap.h"
#include "motionicon.h"
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

class PlaybackBar : public QWidget {
public:
    using QWidget::QWidget;
    QVBoxLayout *stack = nullptr;
    QHBoxLayout *transport = nullptr;
    QWidget *trim = nullptr;
    QWidget *volume = nullptr;
    QSize minimumSizeHint() const override { return {0, QWidget::minimumSizeHint().height()}; }
    void adapt() {
        if (!trim || m_adapting) return;
        m_adapting = true;
        int needed = trim->minimumSizeHint().width() + 24;
        for (int i = 0; i < transport->count(); ++i) {
            auto *item = transport->itemAt(i);
            if (item->widget() == trim) continue;
            needed += (item->widget() == volume ? volume->sizeHint().width() : item->minimumSize().width())
                + transport->spacing();
        }
        const bool wide = width() >= needed;
        if (wide != m_wide) {
            m_wide = wide;
            if (wide) { stack->removeWidget(trim); transport->insertWidget(3, trim); }
            else { transport->removeWidget(trim); stack->addWidget(trim); }
            volume->setVisible(wide);
            updateGeometry();
        }
        m_adapting = false;
    }
protected:
    bool event(QEvent *event) override {
        const bool result = QWidget::event(event);
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest) adapt();
        return result;
    }
private:
    bool m_wide = true, m_adapting = false;
};

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
    // An open Studio popover is deleted with us; its close handler must not run
    // against a half-destroyed window.
    if (m_studioPopover) QObject::disconnect(m_studioPopover, nullptr, this, nullptr);
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
    fl->setContentsMargins(4, 4, 4, 4);
    fl->setSpacing(2);
    auto *zoomControls = new QHBoxLayout;
    zoomControls->setSpacing(2);
    fl->addLayout(zoomControls, 0, 0, Qt::AlignLeft);
    auto *fit = new QToolButton(viewControls);
    fit->setObjectName(QStringLiteral("ZoomFit"));
    fit->setText(QStringLiteral("Fit"));
    fit->setToolTip(QStringLiteral("Fit to window\t0"));
    fit->setAccessibleName(QStringLiteral("Fit to window"));
    fit->setFocusPolicy(Qt::NoFocus);
    fit->setCursor(Qt::PointingHandCursor);
    fit->setFixedHeight(theme::kBarButton.height());
    connect(fit, &QToolButton::clicked, m_canvas, &Canvas::fitMedia);
    zoomControls->addWidget(fit);
    auto *zoom = new QToolButton(viewControls);
    zoom->setObjectName(QStringLiteral("ZoomActual"));
    zoom->setToolTip(QStringLiteral("Actual size\t1"));
    zoom->setAccessibleName(QStringLiteral("Actual size"));
    zoom->setFocusPolicy(Qt::NoFocus);
    zoom->setCursor(Qt::PointingHandCursor);
    zoom->setFixedSize(52, theme::kBarButton.height());
    connect(zoom, &QToolButton::clicked, m_canvas, &Canvas::resetZoom);
    const auto updateZoom = [this, zoom] {
        zoom->setText(QStringLiteral("%1%").arg(qRound(m_canvas->zoom() * 100)));
    };
    connect(m_canvas, &Canvas::viewChanged, zoom, updateZoom);
    updateZoom();
    zoomControls->addWidget(zoom);
    // Align independently within the full row, not the space left after zoom.
    fl->addWidget(m_dragPill, 0, 0, Qt::AlignCenter);
    if (isVideo()) {
        m_exportStatus = new QLabel(viewControls);
        m_exportStatus->setObjectName(QStringLiteral("VideoExportStatus"));
        m_exportStatus->setFixedWidth(110);
        m_exportStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_exportStatus->setToolTip(tr("Video export status"));
        m_exportCancel = new QToolButton(viewControls);
        m_exportCancel->setObjectName(QStringLiteral("VideoExportCancel"));
        m_exportCancel->setText(tr("Cancel"));
        m_exportCancel->setToolTip(tr("Stop the running video export"));
        m_exportCancel->setAccessibleName(tr("Cancel video export"));
        m_exportCancel->setFocusPolicy(Qt::NoFocus);
        m_exportCancel->setCursor(Qt::PointingHandCursor);
        m_exportCancel->setFixedHeight(theme::kBarButton.height());
        m_exportCancel->hide();
        connect(m_exportCancel, &QToolButton::clicked, this, &EditorWindow::cancelVideoExport);
        auto *exportControls = new QHBoxLayout;
        exportControls->setSpacing(2);
        exportControls->addWidget(m_exportStatus);
        exportControls->addWidget(m_exportCancel);
        fl->addLayout(exportControls, 0, 0, Qt::AlignRight);
    }
    viewControls->setAttribute(Qt::WA_StyledBackground, true);
    lay->addWidget(viewControls, isVideo() ? 3 : 2, 0, 1, 2);
    m_canvas->setAnimationsEnabled(cfg.animations);
    m_tools->setAnimationsEnabled(cfg.animations);
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
    // Holding Save opens the export popover; its footer holds the project actions.
    m_exportPanel = new ExportPanel;
    m_exportPanel->setVideo(isVideo());
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
        connect(m_fragmentBar, &FragmentBar::cutToggled, this, [this] {
            const int i = m_selectedFragment;
            if (i < 0) return;
            QVector<Fragment> list = m_studio.fragments;
            if (!fragments::setRemoved(list, i, !list[i].removed)
                || TimeMap(m_media.video.durationMs, m_trimInMs, m_trimOutMs, list).pieces().isEmpty()) {
                m_toast->showMessage(tr("Something of the clip has to stay"));
                return;
            }
            editStudio([&](StudioDocument &d) { d.fragments = list; });
        });
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
    if (cfg.animations) setWindowOpacity(0.0);   // entrance fade starts transparent

    // Size the canvas to the image at 100%; only oversized media starts fitted.
    const int maxW = 1700, maxH = 1000;
    const int chromeW = m_toolbar->toolRail()->sizeHint().width();
    const int chromeH = m_toolbar->sizeHint().height() + viewControls->sizeHint().height()
        + (isVideo() ? findChild<QWidget *>(QStringLiteral("PlaybackBar"))->sizeHint().height() : 0);
    const int minW = qMax(isVideo() ? 520 : 760, lay->minimumSize().width());
    setMinimumSize(minW, chromeH + m_toolbar->toolRail()->sizeHint().height());
    resize(qMin(qMax(native.width() + chromeW, minW), maxW), qMin(native.height() + chromeH, maxH));
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
    QTimer::singleShot(0, this, &EditorWindow::offerKeptEdit);
    if (!m_cfg.animations) { setWindowOpacity(1.0); return; }
    auto *a = new QPropertyAnimation(this, "windowOpacity", this);
    a->setDuration(150); a->setStartValue(0.0); a->setEndValue(1.0);
    a->setEasingCurve(QEasingCurve::OutCubic);
    a->start(QAbstractAnimation::DeleteWhenStopped);
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
    flushRecovery();
    QWidget::closeEvent(e);
}

QWidget *EditorWindow::createPlaybackBar() {
    auto *bar = new PlaybackBar(this);
    bar->setObjectName("PlaybackBar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto *lay = new QVBoxLayout(bar);
    lay->setContentsMargins(12, 4, 12, 2);
    lay->setSpacing(2);
    auto *playback = new QHBoxLayout;
    playback->setSpacing(6);
    auto *trimControls = new QWidget(bar);
    trimControls->setObjectName(QStringLiteral("TrimControls"));
    auto *trim = new QHBoxLayout(trimControls);
    trim->setContentsMargins(0, 0, 0, 0);
    trim->setSpacing(0);   // captions and values bring their own 6px padding

    m_playButton = new QToolButton(bar);
    const QColor iconColor = palette().color(QPalette::ButtonText);
    m_playButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/play.svg"), iconColor, iconColor));
    m_playButton->setObjectName("PlaybackPlay");
    m_playButton->setAutoRaise(true);
    m_playButton->setFocusPolicy(Qt::NoFocus);
    m_playButton->setCursor(Qt::PointingHandCursor);
    m_playButton->setFixedSize(theme::kBarButton);
    m_playButton->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_playButton->setToolTip(QStringLiteral("Play / Pause\tSpace / K"));
    m_playButton->setAccessibleName(m_playButton->toolTip());
    m_timeLabel = new QLabel(QStringLiteral("0:00 / ") + formatTime(m_media.video.durationMs), bar);
    m_timeLabel->setObjectName("PlaybackTime");
    m_timeLabel->ensurePolished();
    m_timeLabel->setFixedWidth(m_timeLabel->fontMetrics().horizontalAdvance(
        formatPreciseTime(m_media.video.durationMs) + QStringLiteral(" / ") + formatTime(m_media.video.durationMs)));

    m_muteButton = new QToolButton(bar);
    m_muteButton->setObjectName("PlaybackMute");
    m_muteButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/volume.svg"), iconColor, iconColor));
    m_muteButton->setAutoRaise(true);
    m_muteButton->setFocusPolicy(Qt::NoFocus);
    m_muteButton->setCursor(Qt::PointingHandCursor);
    m_muteButton->setFixedSize(theme::kBarButton);
    m_muteButton->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_muteButton->setToolTip(tr("Mute audio\nHold for volume"));
    m_muteButton->setAccessibleName(m_muteButton->toolTip());

    m_volumeSlider = new QSlider(Qt::Horizontal, bar);
    m_volumeSlider->setObjectName("PlaybackVolume");
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(60);
    m_volumeSlider->setToolTip(QStringLiteral("Volume"));
    m_volumeSlider->setAccessibleName(QStringLiteral("Volume"));
    auto *audioMenu = new QMenu(m_muteButton);
    auto *audioWidget = new QWidget(audioMenu);
    auto *audioLayout = new QHBoxLayout(audioWidget);
    audioLayout->setContentsMargins(8, 8, 8, 8);
    auto *popupVolume = new QSlider(Qt::Horizontal, audioWidget);
    popupVolume->setRange(0, 100);
    popupVolume->setValue(100);
    popupVolume->setFixedWidth(100);
    popupVolume->setAccessibleName(tr("Volume"));
    audioLayout->addWidget(popupVolume);
    auto *audioAction = new QWidgetAction(audioMenu);
    audioAction->setDefaultWidget(audioWidget);
    audioMenu->addAction(audioAction);
    m_muteButton->setMenu(audioMenu);
    m_muteButton->setPopupMode(QToolButton::DelayedPopup);
    m_muteButton->setFocusPolicy(Qt::StrongFocus);
    connect(popupVolume, &QSlider::valueChanged, m_volumeSlider, &QSlider::setValue);
    connect(m_volumeSlider, &QSlider::valueChanged, popupVolume, &QSlider::setValue);

    playback->addWidget(m_playButton);
    playback->addWidget(m_timeLabel);
    playback->addStretch(1);
    m_timeline = new VideoTimeline(bar);
    m_timeline->setDuration(m_media.video.durationMs);
    m_timeline->setMinimumRange(m_media.video.fps > 0.0
        ? qMax<qint64>(1, qRound64(1000.0 / m_media.video.fps)) : 1);
    m_timeline->setTrimRange(m_trimInMs, m_trimOutMs);
    m_trimInLabel = new QLineEdit(formatPreciseTime(m_trimInMs), bar);
    m_trimInLabel->setObjectName(QStringLiteral("TrimInTime"));
    m_trimOutLabel = new QLineEdit(formatPreciseTime(m_trimOutMs), bar);
    m_trimOutLabel->setObjectName(QStringLiteral("TrimOutTime"));
    m_trimDurationLabel = new QLabel(bar);
    m_trimDurationLabel->setObjectName(QStringLiteral("TrimDuration"));
    m_trimDurationLabel->setToolTip(QStringLiteral("Selected duration"));
    for (auto *field : {m_trimInLabel, m_trimOutLabel}) {
        field->ensurePolished();
        field->setMaxLength(20);
        field->setFixedHeight(theme::kBarButton.height());
        field->setAccessibleName(field == m_trimInLabel ? tr("Trim start") : tr("Trim end"));
        field->setToolTip(field == m_trimInLabel
            ? tr("Start of the exported clip\nApply\tEnter\nCancel\tEsc")
            : tr("End of the exported clip\nApply\tEnter\nCancel\tEsc"));
        connect(field, &QLineEdit::editingFinished, this, [this, field] { commitTrimTime(field); });
    }
    updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
    auto makeTrimButton = [bar](const QString &text, const QString &name) {
        auto *button = new QToolButton(bar);
        button->setText(text);
        button->setObjectName(name);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(theme::kBarButton.height());
        return button;
    };
    auto *setIn = makeTrimButton(tr("Start"), QStringLiteral("TrimSetIn"));
    auto *setOut = makeTrimButton(tr("End"), QStringLiteral("TrimSetOut"));
    auto *reset = makeTrimButton({}, QStringLiteral("TrimReset"));
    reset->setIcon(theme::tintedIcon(QStringLiteral(":/icons/reset.svg"), iconColor, iconColor));
    reset->setFixedWidth(theme::kBarButton.width());
    reset->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    setIn->setToolTip(tr("Set start to playhead\tI"));
    setOut->setToolTip(tr("Set end to playhead\tO"));
    setIn->setAccessibleName(setIn->toolTip());
    setOut->setAccessibleName(setOut->toolTip());
    reset->setToolTip(tr("Reset trim to the complete clip"));
    reset->setAccessibleName(reset->toolTip());

    trim->addWidget(setIn);
    trim->addWidget(m_trimInLabel);
    trim->addSpacing(12);
    trim->addWidget(setOut);
    trim->addWidget(m_trimOutLabel);
    trim->addSpacing(12);
    auto *durationCaption = new QLabel(tr("Duration"), trimControls);
    durationCaption->setObjectName(QStringLiteral("TrimDurationCaption"));
    trim->addWidget(durationCaption);
    trim->addWidget(m_trimDurationLabel);
    trim->addSpacing(6);
    trim->addWidget(reset);
    trim->addStretch(1);
    playback->addWidget(trimControls);
    playback->addStretch(1);
    m_loopButton = new QToolButton(bar);
    m_loopButton->setObjectName(QStringLiteral("PlaybackLoop"));
    m_loopButton->setCheckable(true);
    m_loopButton->setAutoRaise(true);
    m_loopButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/loop.svg"), iconColor, iconColor));
    m_loopButton->setIconSize(QSize(theme::kIconSize, theme::kIconSize));
    m_loopButton->setFixedSize(theme::kBarButton);
    m_loopButton->setToolTip(tr("Loop the selected range"));
    m_loopButton->setAccessibleName(m_loopButton->toolTip());
    m_speedButton = new QToolButton(bar);
    m_speedButton->setObjectName(QStringLiteral("PlaybackSpeed"));
    m_speedButton->setText(QStringLiteral("1×"));
    m_speedButton->setFixedHeight(theme::kBarButton.height());
    m_speedButton->setToolTip(tr("Preview speed"));
    m_speedButton->setAccessibleName(m_speedButton->toolTip());
    m_speedButton->setPopupMode(QToolButton::InstantPopup);
    auto *rates = new QMenu(m_speedButton);
    rates->setObjectName(QStringLiteral("PlaybackRateMenu"));
    rates->setWindowFlag(Qt::FramelessWindowHint);
    rates->setAttribute(Qt::WA_TranslucentBackground);
    auto *rateGroup = new QActionGroup(rates);
    for (qreal rate : {0.25, 0.5, 1.0, 1.5, 2.0}) {
        auto *action = rates->addAction(QStringLiteral("%1×").arg(rate));
        action->setCheckable(true);
        action->setChecked(rate == 1.0);
        action->setData(rate);
        rateGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, rate] {
            ensureVideoPlayer();
            m_previewRate = rate;
            theme::setMenuLabel(m_speedButton, QStringLiteral("%1×").arg(rate));
            for (auto *other : m_speedButton->menu()->actions())
                other->setChecked(qFuzzyCompare(other->data().toDouble(), rate));
            if (m_player) m_player->setPlaybackRate(rate);
            if (m_player) followPlaybackPieces(m_player->position());
        });
    }
    m_speedButton->setMenu(rates);
    theme::setMenuArrow(m_speedButton);
    playback->addWidget(m_loopButton);
    playback->addWidget(m_speedButton);
    playback->addWidget(m_muteButton);
    playback->addWidget(m_volumeSlider);
    lay->addWidget(m_timeline);
    lay->addLayout(playback);

    m_previewProvider = new VideoPreviewProvider(m_media.path, this);
    m_videoPreview = new QWidget(this);
    m_videoPreview->setObjectName(QStringLiteral("VideoPreview"));
    m_videoPreview->setAttribute(Qt::WA_StyledBackground);
    m_videoPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *previewLayout = new QVBoxLayout(m_videoPreview);
    previewLayout->setContentsMargins(4, 4, 4, 4);
    previewLayout->setSpacing(2);
    m_previewImage = new QLabel(m_videoPreview);
    m_previewImage->setFixedSize(192, 108);
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewTime = new QLabel(m_videoPreview);
    m_previewTime->setTextFormat(Qt::RichText);
    m_previewTime->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewImage);
    previewLayout->addWidget(m_previewTime);
    m_videoPreview->hide();
    auto *previewOpacity = new QGraphicsOpacityEffect(m_videoPreview);
    previewOpacity->setOpacity(1);
    m_videoPreview->setGraphicsEffect(previewOpacity);
    m_previewFade = new QPropertyAnimation(previewOpacity, "opacity", m_videoPreview);
    m_previewFade->setDuration(100);
    m_previewFade->setStartValue(0.0);
    m_previewFade->setEndValue(1.0);
    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(150);
    connect(m_previewTimer, &QTimer::timeout, this, &EditorWindow::showVideoPreview);
    m_stripTimer = new QTimer(this);
    m_stripTimer->setSingleShot(true);
    m_stripTimer->setInterval(100);
    connect(m_stripTimer, &QTimer::timeout, this, &EditorWindow::scheduleContactSheetLoad);
    connect(m_timeline, &VideoTimeline::viewRangeChanged, this, [this] {
        hideVideoPreview();
        m_stripTimer->start();
    });
    connect(m_timeline, &VideoTimeline::hoverRequested, this, [this](qint64 time, QPoint point) {
        if (time != m_hoverTime) {
            qint64 sampled = -1;
            const QImage nearby = m_timeline->thumbnailNear(time, &sampled);
            if (!nearby.isNull()) {
                setVideoPreviewImage(nearby);
                m_previewSampleTime = sampled;
            } else {
                m_previewImage->setText(QStringLiteral("…"));
                m_previewSampleTime = -1;
            }
        }
        m_hoverTime = time;
        m_hoverPoint = point;
        if (m_videoPreview->isVisible() || m_timeline->trimming()) showVideoPreview();
        else if (!m_previewTimer->isActive()) m_previewTimer->start();
    });
    connect(m_timeline, &VideoTimeline::hoverLeft, this, &EditorWindow::hideVideoPreview);
    connect(m_previewProvider, &VideoPreviewProvider::thumbnailReady,
            m_timeline, &VideoTimeline::setThumbnail);
    connect(m_previewProvider, &VideoPreviewProvider::hoverReady, this,
            [this](qint64 time, const QImage &image) {
        if (time != m_hoverTime || !m_videoPreview->isVisible()) return;
        setVideoPreviewImage(image);
        m_previewSampleTime = time;
        m_previewTime->setText(formatPreciseTime(time));
    });
    connect(m_previewProvider, &VideoPreviewProvider::hoverFailed, this, [this](qint64 time) {
        if (time == m_hoverTime && m_previewSampleTime < 0) m_previewImage->setText(tr("Preview unavailable"));
    });

    connect(m_playButton, &QToolButton::clicked, this, &EditorWindow::togglePlayback);
    connect(m_muteButton, &QToolButton::clicked, this, [this]{
        ensureVideoPlayer();
        if (!m_audioOutput) return;
        const bool muted = !m_audioOutput->isMuted();
        m_audioOutput->setMuted(muted);
        m_muteButton->setIcon(theme::tintedIcon(
            muted ? QStringLiteral(":/icons/muted.svg") : QStringLiteral(":/icons/volume.svg"),
            palette().color(QPalette::ButtonText), palette().color(QPalette::ButtonText)));
        m_muteButton->setToolTip(muted ? tr("Unmute audio\nHold for volume") : tr("Mute audio\nHold for volume"));
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
            m_muteButton->setToolTip(tr("Mute audio\nHold for volume"));
            m_muteButton->setAccessibleName(m_muteButton->toolTip());
        }
    });
    m_seekTimer = new QTimer(this);
    m_seekTimer->setSingleShot(true);
    m_seekTimer->setInterval(33);
    connect(m_seekTimer, &QTimer::timeout, this, &EditorWindow::flushVideoSeek);
    m_seekSettleTimer = new QTimer(this);
    m_seekSettleTimer->setSingleShot(true);
    m_seekSettleTimer->setInterval(2000);
    connect(m_seekSettleTimer, &QTimer::timeout, this, [this] {
        m_resumeAfterSeek = false;
        m_seekSettling = false;
        m_loopSeeking = false;
        m_seekTarget = -1;
        if (m_copyFramePending) {
            m_copyFramePending = false;
            if (m_toast) m_toast->showMessage(tr("Frame unavailable; try again after seeking"));
        }
    });
    connect(m_timeline, &VideoTimeline::interactionStarted, this, [this](bool trimming) {
        m_timelineTrimming = trimming;
        ensureVideoPlayer();
        m_timelineActive = true;
        m_resumeAfterSeek = !trimming && m_player
            && m_player->playbackState() == QMediaPlayer::PlayingState;
        if (m_player) m_player->pause();
        if (m_videoExportTimer) m_videoExportTimer->stop();
    });
    connect(m_timeline, &VideoTimeline::seekRequested, this, &EditorWindow::requestVideoSeek);
    connect(m_timeline, &VideoTimeline::interactionFinished, this, [this](bool cancelled) {
        m_timelineActive = false;
        // A click on the film strip selects the fragment under the playhead.
        if (!cancelled && !m_timelineTrimming && m_studio.fragments.size() > 1)
            selectFragment(fragments::indexAt(m_studio.fragments, m_timeline->position()));
        if (cancelled) m_resumeAfterSeek = m_copyFramePending = false;
        flushVideoSeek();
        if (hasVideoEdits() && m_cachedVideoRevision != m_videoRevision) scheduleVideoExportCache();
    });
    connect(m_timeline, &VideoTimeline::trimCommitted,
            this, &EditorWindow::applyTrimRange);
    connect(m_timeline, &VideoTimeline::trimPreviewed,
            this, &EditorWindow::updateTrimTimeLabels);
    connect(m_timeline, &VideoTimeline::zoomAddRequested, this, &EditorWindow::addZoomAt);
    connect(m_timeline, &VideoTimeline::zoomSelected, this, &EditorWindow::selectZoom);
    connect(m_timeline, &VideoTimeline::cutClicked, this, &EditorWindow::selectFragment);
    connect(m_timeline, &VideoTimeline::zoomsPreviewed, this, [this](const QVector<ZoomSegment> &zooms) {
        m_studio.zooms = zooms;   // the drag's own preview; the edit is one undo step at release
        if (!m_cameraRebuild->isActive()) m_cameraRebuild->start();
    });
    connect(m_timeline, &VideoTimeline::zoomsEdited, this,
            [this](const QVector<ZoomSegment> &before, const QVector<ZoomSegment> &after) {
        StudioDocument from = m_studio, to = m_studio;
        from.zooms = before;
        to.zooms = after;
        m_undo->push(new SetStudioDocumentCommand(from, to,
            [this](const StudioDocument &d) { setStudioDocument(d); }));
    });
    connect(m_timeline, &VideoTimeline::zoomMenuRequested, this, [this](quint32 id, QPoint global) {
        selectZoom(id);
        QMenu menu(this);
        const QColor ink = palette().color(QPalette::WindowText);
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                            ZoomSegment::Motion::Instant}) {
            QAction *action = menu.addAction(motionIcon(motion, ink, theme::kFsSmall), motionName(motion));
            connect(action, &QAction::triggered, this, [this, id, motion] {
                editZoom(id, [motion](ZoomSegment &z) { z.motion = motion; });
            });
        }
        menu.addSeparator();
        connect(menu.addAction(tr("Remove zoom\tDelete")), &QAction::triggered, this,
                [this, id] { removeZoom(id); });
        menu.exec(global);
    });
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
    bar->stack = lay; bar->transport = playback; bar->trim = trimControls; bar->volume = m_volumeSlider;
    bar->adapt();
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

void EditorWindow::togglePlayback() {
    ensureVideoPlayer();
    if (!m_player) return;
    if (m_timelineActive) return;
    if (m_seekSettling) { m_resumeAfterSeek = !m_resumeAfterSeek; return; }
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        if (m_player->position() < m_trimInMs || m_player->position() >= m_trimOutMs)
            m_player->setPosition(m_trimInMs);
        m_player->play();
    }
}

void EditorWindow::handlePlaybackEnd() {
    if (!m_player || m_loopSeeking || m_timelineActive || m_seekSettling) return;
    m_player->pause();
    m_loopSeeking = true;
    m_resumeAfterSeek = m_loopButton->isChecked();
    const qint64 lastFrame = m_media.video.fps > 0
        ? qRound64((qCeil(m_trimOutMs * m_media.video.fps / 1000.0) - 1) * 1000.0 / m_media.video.fps)
        : m_trimOutMs - 1;
    requestVideoSeek(m_resumeAfterSeek ? m_trimInMs : qMax(m_trimInMs, lastFrame));
    m_timeline->setPosition(m_seekTarget);
    flushVideoSeek();
}

bool EditorWindow::eventFilter(QObject *object, QEvent *event) {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || (widget != this && !isAncestorOf(widget)))
        return QWidget::eventFilter(object, event);
    if (event->type() == QEvent::KeyPress && m_crop && m_crop->active()
        && !qobject_cast<QMenu *>(widget) && !qobject_cast<QLineEdit *>(widget)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            if (m_crop->dragging()) m_crop->cancelDrag(); else m_crop->cancel();
            return true;
        }
        if (m_crop->dragging() && ((key->modifiers().testFlag(Qt::ControlModifier)
            && (key->key() == Qt::Key_C || key->key() == Qt::Key_S))
            || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
            return true;
    }
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        auto *button = qobject_cast<QToolButton *>(widget);
        if (button && button->isEnabled() && button->menu()
            && key->key() == Qt::Key_Down && key->modifiers() == Qt::AltModifier) {
            m_tooltip->hide();
            if (!key->isAutoRepeat()) button->showMenu();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress && (widget == m_trimInLabel || widget == m_trimOutLabel)) {
        const auto *key = static_cast<QKeyEvent *>(event);
        auto *field = static_cast<QLineEdit *>(widget);
        if (key->key() == Qt::Key_Escape) {
            field->setText(formatPreciseTime(field == m_trimInLabel ? m_trimInMs : m_trimOutMs));
            field->setModified(false);
            m_canvas->setFocus();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            commitTrimTime(field);
            return true;
        }
    }
    if (event->type() == QEvent::ToolTip && !widget->toolTip().isEmpty()) {
        if (widget == m_timeline) return true; // The timeline has its own image/time hint.
        const auto *help = static_cast<QHelpEvent *>(event);
        m_tooltip->setText(theme::tooltipHtml(widget->toolTip()));
        m_tooltip->adjustSize();
        QPoint pos = widget->mapTo(this, help->pos()) + QPoint(10, 18);
        if (pos.y() + m_tooltip->height() > height() - 4)
            pos.setY(widget->mapTo(this, help->pos()).y() - m_tooltip->height() - 8);
        pos.setX(qBound(4, pos.x(), qMax(4, width() - m_tooltip->width() - 4)));
        pos.setY(qBound(4, pos.y(), qMax(4, height() - m_tooltip->height() - 4)));
        m_tooltip->move(pos);
        m_tooltipOwner = widget;
        m_tooltip->show();
        m_tooltip->raise();
        m_tooltipTimer->start(5000);
        return true;
    }
    if (event->type() == QEvent::MouseButtonPress) m_spaceConsumed = true;
    if (event->type() == QEvent::FocusOut) {
        m_spaceArmed = false;
        m_canvas->cancelPan();
    }
    if (event->type() == QEvent::WindowDeactivate && widget == this) {
        if (m_crop && m_crop->active()) m_crop->release();
        m_spaceArmed = false;
        m_canvas->cancelPan();
        if (m_timeline) m_timeline->cancelInteraction();
        hideVideoPreview();
    }
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::KeyPress
        || event->type() == QEvent::WindowDeactivate
        || (event->type() == QEvent::Leave && widget == m_tooltipOwner))
        m_tooltip->hide();
    return QWidget::eventFilter(object, event);
}

void EditorWindow::scheduleContactSheetLoad() {
    if (!m_previewProvider || !isVisible()) return;
    m_previewProvider->requestStrip(m_timeline->thumbnailTimes(), QSize(128, 72) * devicePixelRatioF());
}

void EditorWindow::hideVideoPreview() {
    if (!m_videoPreview) return;
    m_hoverTime = -1;
    m_previewSampleTime = -1;
    m_previewTimer->stop();
    m_videoPreview->hide();
    m_previewProvider->cancelHover();
}

void EditorWindow::showVideoPreview() {
    if (m_hoverTime < 0 || !isVisible()) return;
    const bool appearing = m_videoPreview->isHidden();
    // The nearest decoded frame, when it differs, follows a step quieter.
    m_previewTime->setText(m_previewSampleTime >= 0 && m_previewSampleTime != m_hoverTime
        ? QStringLiteral("%1&nbsp;&nbsp;<span style=\"color:%2\">~%3</span>")
              .arg(formatPreciseTime(m_hoverTime), palette().color(QPalette::PlaceholderText).name(),
                   formatPreciseTime(m_previewSampleTime))
        : formatPreciseTime(m_hoverTime));
    m_videoPreview->adjustSize();
    QPoint pos = m_timeline->mapTo(this, m_hoverPoint);
    pos.setX(qBound(4, pos.x() - m_videoPreview->width() / 2,
                    qMax(4, width() - m_videoPreview->width() - 4)));
    pos.setY(qMax(4, m_timeline->mapTo(this, QPoint()).y() - m_videoPreview->height() - 5));
    m_videoPreview->move(pos);
    m_videoPreview->show();
    m_videoPreview->raise();
    if (appearing && m_cfg.animations) m_previewFade->start();
    m_previewProvider->requestHover(m_hoverTime, QSize(256, 144) * devicePixelRatioF());
}

void EditorWindow::setVideoPreviewImage(const QImage &image) {
    QPixmap pixmap = QPixmap::fromImage(image).scaled(m_previewImage->size() * devicePixelRatioF(),
        Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_previewImage->setPixmap(pixmap);
}

bool EditorWindow::updateVideoBackground() {
    if (m_videoBackgroundCurrent) return true;
    const auto &frame = m_lastVideoFrame;
    QImage image = frame.toImage();
    if (image.isNull()) return false;
    // Qt < 6.8 includes rotation/mirroring in toImage(). Newer Qt
    // applies the surface transform there, but not the presentation one.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const int rotation = int(frame.rotation());
    if (rotation) image = image.transformed(QTransform().rotate(rotation));
    if (frame.mirrored()) image = image.transformed(QTransform().scale(-1, 1));
#endif
    if (image.size() != m_media.nativeSize())
        image = image.scaled(m_media.nativeSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_bg = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_tools->setBackground(m_bg);
    m_ocr->setBackground(m_bg);
    for (QGraphicsItem *item : m_scene->items())
        if (auto *redact = dynamic_cast<RedactItem *>(item))
            redact->setSource(m_bg);
    m_videoBackgroundCurrent = true;
    return true;
}

void EditorWindow::showVideoStill() {
    if (!m_lastVideoFrame.isValid() || !updateVideoBackground()) return;
    m_videoStill->setPixmap(QPixmap::fromImage(m_bg));
    m_videoStill->show();
}

void EditorWindow::ensureVideoPlayer() {
    if (!isVideo() || m_player) return;
    if (!m_videoItem) {
        auto *videoItem = new PreviewVideoItem;
        videoItem->setSize(QSizeF(m_media.nativeSize()));
        videoItem->setAspectRatioMode(Qt::IgnoreAspectRatio);
        videoItem->setZValue(-1000);
        m_scene->addItem(videoItem);
        if (m_backgroundItem) {
            m_scene->removeItem(m_backgroundItem);
            delete m_backgroundItem;
        }
        m_videoItem = videoItem;
        m_backgroundItem = m_videoItem;
        // Keep paused frames independent of the backend's video surface. Making
        // the still a background child also excludes it from annotation exports.
        m_videoStill = new PreviewPixmapItem(videoItem);
        m_videoStill->setZValue(-1000);
        m_videoStill->setAcceptedMouseButtons(Qt::NoButton);
        m_videoStill->setTransformationMode(Qt::SmoothTransformation);
        connect(videoItem->videoSink(), &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) {
            if (!frame.isValid()) return;
            m_lastVideoFrame = frame;
            m_videoBackgroundCurrent = false;
            bool needsPixels = m_tools->tool() == ToolType::Redact;
            if (!needsPixels)
                for (QGraphicsItem *item : m_scene->items())
                    if (dynamic_cast<RedactItem *>(item)) { needsPixels = true; break; }
            if (needsPixels) updateVideoBackground();
            if (m_player->playbackState() == QMediaPlayer::PlayingState)
                m_videoStill->hide();
            else
                showVideoStill();
            // GStreamer's buffer PTS may include a stream offset (e.g. H.264
            // reordering delay), while QMediaPlayer positions start at zero.
            // Loading primes the initial still. Anchor that first frame before
            // any explicit seek; the asynchronous player clock may already tick.
            if (!m_hasVideoFrame && !m_hasSentVideoSeek && frame.startTime() >= 0)
                m_frameTimeOrigin = frame.startTime() / 1000;
            m_hasVideoFrame = true;
            m_presentedStart = frame.startTime() < 0 ? -1 : frame.startTime() / 1000 - m_frameTimeOrigin;
            m_presentedEnd = frame.endTime() < 0 ? -1 : frame.endTime() / 1000 - m_frameTimeOrigin;
            if (!m_timelineActive && !m_seekSettling && m_presentedStart >= 0
                && m_player->playbackState() == QMediaPlayer::PlayingState)
                m_timeline->setPosition(m_presentedStart);
            if (m_seekSettling && m_presentedStart >= 0 && m_seekTarget >= m_presentedStart - 1
                && m_seekTarget < (m_presentedEnd > m_presentedStart ? m_presentedEnd
                    : m_presentedStart + qMax<qint64>(1, qRound64(1000 / qMax(1.0, m_media.video.fps)))))
                finishVideoSeek();
            updateCamera();
            if (m_presentedStart >= 0 && m_player->playbackState() == QMediaPlayer::PlayingState)
                followPlaybackPieces(m_presentedStart);
            if (m_miniMap && m_miniMap->isVisible() && updateVideoBackground())
                m_miniMap->setContent(m_bg.copy(cameraContent())
                    .scaled(304, 224, Qt::KeepAspectRatio, Qt::SmoothTransformation), QRectF(cameraContent()));
        });
    }
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_audioOutput->setVolume(m_volumeSlider ? m_volumeSlider->value() / 100.0f : 1.0f);
    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoOutput(m_videoItem);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state){
        if (state != QMediaPlayer::PlayingState) showVideoStill();
        updateCamera();
        refreshZoomUi();
        if (m_playButton) {
            const QColor color = palette().color(QPalette::ButtonText);
            m_playButton->setIcon(theme::tintedIcon(
                state == QMediaPlayer::PlayingState
                    ? QStringLiteral(":/icons/pause.svg") : QStringLiteral(":/icons/play.svg"),
                color, color));
        }
    });
    connect(m_player, &QMediaPlayer::playbackRateChanged, this, [this](qreal) {
        // The label shows the preview rate; a fragment's own speed multiplies it.
        theme::setMenuLabel(m_speedButton, QStringLiteral("%1×").arg(m_previewRate));
        for (auto *action : m_speedButton->menu()->actions())
            action->setChecked(qFuzzyCompare(action->data().toDouble(), m_previewRate));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) handlePlaybackEnd();
        if (status == QMediaPlayer::LoadedMedia) {
            // Retry after loading for backends that cannot preroll during setSource.
            // Queue it so a pending user play/seek request takes precedence.
            QTimer::singleShot(0, this, [this] {
                if (!m_hasVideoFrame && !m_hasSentVideoSeek
                    && m_player->playbackState() == QMediaPlayer::StoppedState)
                    m_player->pause();
            });
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
        m_media.video.durationMs = duration;
        m_trimInMs = m_timeline->trimIn();
        m_trimOutMs = m_timeline->trimOut();
        updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
        rebuildCamera();
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(m_player->position()) + QStringLiteral(" / ") + formatTime(duration));
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos){
        if (m_timelineActive || m_seekSettling) return;
        if (m_timeline && (m_presentedStart < 0 || m_player->playbackState() != QMediaPlayer::PlayingState))
            m_timeline->setPosition(pos);
        if (m_timeLabel)
            m_timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") + formatTime(m_player->duration()));
        if (m_player->playbackState() == QMediaPlayer::PlayingState) followPlaybackPieces(pos);
        if (m_player->playbackState() == QMediaPlayer::PlayingState && pos >= m_trimOutMs) {
            handlePlaybackEnd();
        }
        if (m_player->playbackState() == QMediaPlayer::PlayingState && m_hoverTime < 0
            && (pos < m_timeline->visibleStart() || pos > m_timeline->visibleEnd()))
            m_timeline->ensureVisible(pos);
    });
    connect(m_player, &QMediaPlayer::seekableChanged, this, [this](bool seekable) {
        if (seekable && m_seekTarget >= 0) flushVideoSeek();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this] {
        m_seekTimer->stop();
        m_seekSettleTimer->stop();
        m_seekSettling = m_loopSeeking = m_resumeAfterSeek = false;
        m_seekTarget = -1;
        if (m_copyFramePending) {
            m_copyFramePending = false;
            if (m_toast) m_toast->showMessage(tr("Frame unavailable"));
        }
    });
    m_player->setSource(QUrl::fromLocalFile(m_media.path));
    m_player->pause(); // Decode the initial still and establish its time origin before editing.
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

void EditorWindow::doUndo() { cancelGestures(); m_undo->undo(); refreshRedactBar(); }
void EditorWindow::doRedo() { cancelGestures(); m_undo->redo(); refreshRedactBar(); }

// Undo and redo land on a settled document: no gesture keeps a stale copy.
void EditorWindow::cancelGestures() {
    if (m_crop && m_crop->active()) m_crop->cancel();
    m_ocr->cancel();
    if (m_timeline && m_timeline->zoomDragging()) m_timeline->cancelInteraction();
    if (m_canvas->cameraDragging()) m_canvas->cancelCameraDrag();
    finishCameraGesture(true);
}

void EditorWindow::setupCrop() {
    m_crop = new CropController(this);
    m_cropBar = new CropBar(m_canvas->viewport());
    m_cropBar->setSourceSize(m_media.nativeSize());
    m_cropBar->hide();
    m_canvas->setCropController(m_crop);
    connect(m_crop, &CropController::changed, this, [this] {
        if (!m_crop->active()) return;
        m_cropBar->setOutputSize(m_crop->pixels().size());
        positionCropBar();
    });
    connect(m_canvas, &Canvas::viewChanged, this, &EditorWindow::positionCropBar);
    connect(m_cropBar, &CropBar::ratioChosen, this, [this](qreal ratio) {
        m_crop->setRatio(ratio);
        positionCropBar();
    });
    connect(m_cropBar, &CropBar::resetRequested, this, [this] {
        m_cropBar->resetRatio();
        m_crop->reset();
        positionCropBar();
    });
    connect(m_cropBar, &CropBar::applyRequested, m_crop, &CropController::accept);
    connect(m_cropBar, &CropBar::cancelRequested, m_crop, &CropController::cancel);
    connect(m_crop, &CropController::accepted, this, [this](QRect rect) {
        if (rect == QRect(QPoint(), m_media.nativeSize())) rect = {};
        if (rect != m_cropRect)
            m_undo->push(new SetCropCommand(m_cropRect, rect, [this](QRect value) { setCropRect(value); }));
        else setCropRect(rect);
        m_cropBar->hide();
        m_handles->setVisible(true);
        if (m_tools->tool() == ToolType::Crop) m_tools->setTool(ToolType::Move);
        m_canvas->setFocus();
        if (isVideo() && hasVideoEdits()) scheduleVideoExportCache();
    });
    connect(m_crop, &CropController::cancelled, this, [this] {
        setCropRect(m_cropRect);
        m_canvas->restoreView(m_beforeCropView, m_beforeCropCenter, m_beforeCropFit);
        const auto items = m_scene->items();
        for (auto *item : m_beforeCropSelection)
            if (items.contains(item)) item->setSelected(true);
        m_cropBar->hide();
        m_handles->setVisible(true);
        if (m_tools->tool() == ToolType::Crop) m_tools->setTool(ToolType::Move);
        m_canvas->setFocus();
        if (isVideo() && hasVideoEdits()) scheduleVideoExportCache();
    });
    connect(m_tools, &ToolController::toolChanged, this, [this](ToolType type) {
        if (type != ToolType::Crop) { finishCrop(); return; }
        if (isVideo() && !m_media.video.cropSupported) {
            m_toast->showMessage(tr("Crop is unavailable for this video's display transform"));
            m_tools->setTool(ToolType::Move);
            return;
        }
        m_tools->commitTextEdit();
        m_beforeCropSelection = m_scene->selectedItems();
        m_scene->clearSelection();
        m_scene->clearFocus();
        m_handles->setVisible(false);
        m_canvas->setCamera({});
        refreshZoomUi();
        m_beforeCropView = m_canvas->transform();
        m_beforeCropCenter = m_canvas->mapToScene(m_canvas->viewport()->rect().center());
        m_beforeCropFit = m_canvas->fitted();
        m_canvas->setContentRect({});
        m_cropBar->resetRatio();
        m_crop->begin(m_media.nativeSize(), m_cropRect, isVideo() ? 2 : 1);
        if (m_beforeCropFit) m_canvas->fitMedia();
        m_cropBar->show();
        positionCropBar();
        m_canvas->setFocus();
    });
    if (m_tools->tool() == ToolType::Crop) {
        m_tools->setTool(ToolType::Move);
        QTimer::singleShot(0, this, [this] { m_tools->setTool(ToolType::Crop); });
    }
}

void EditorWindow::finishCrop() {
    if (m_crop && m_crop->active()) m_crop->accept();
}

void EditorWindow::setCropRect(QRect rect) {
    m_cropRect = rect;
    applyContentRect();
    rebuildCamera();
    const QSize size = rect.isEmpty() ? m_media.nativeSize() : rect.size();
    const QString name = m_media.path.isEmpty() ? QStringLiteral("Image") : QFileInfo(m_media.path).fileName();
    setWindowTitle(QStringLiteral("%1 (%2 × %3)")
        .arg(name).arg(size.width()).arg(size.height()));
}

// The canvas shows the base view: the crop, narrowed by "keep zoomed in".
void EditorWindow::applyContentRect() {
    const QRect base = cameraBase();
    m_canvas->setContentRect(base == QRect(QPoint(), m_media.nativeSize()) ? QRect() : base);
    updateStudioPreview();
}

void EditorWindow::positionCropBar() {
    if (!m_crop || !m_crop->active()) return;
    m_cropBar->setAvailableWidth(m_canvas->viewport()->width() - 12);
    const QRect rect = m_canvas->mapFromScene(m_crop->rect()).boundingRect();
    const QSize viewport = m_canvas->viewport()->size();
    int y = rect.bottom() + 12;
    if (y + m_cropBar->height() > viewport.height() - 6) y = rect.top() - m_cropBar->height() - 12;
    m_cropBar->move(qBound(6, rect.center().x() - m_cropBar->width() / 2,
                          qMax(6, viewport.width() - m_cropBar->width() - 6)),
                    qBound(6, y, qMax(6, viewport.height() - m_cropBar->height() - 6)));
    m_cropBar->raise();
}

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
        if (m_loopButton)
            m_loopButton->setIcon(theme::tintedIcon(QStringLiteral(":/icons/loop.svg"), color, color));
        if (m_speedButton) theme::setMenuArrow(m_speedButton);
    }
    m_textBar->refreshTheme();
    m_dragPill->refreshTheme();
    if (m_zoomBar) m_zoomBar->refreshTheme();
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
    finishCrop();
    if (isVideo())
        return renderAnnotationOverlay();
    const auto selection = m_scene->selectedItems();
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the image
    QImage image = renderToImage(*m_scene, m_bg.size());
    for (QGraphicsItem *item : selection) item->setSelected(true);
    return renderStudioImage(m_cropRect.isEmpty() ? image : image.copy(m_cropRect), m_studio.style);
}

QString EditorWindow::configPath() const {
    return m_cli.configPath.isEmpty() ? defaultConfigPath() : m_cli.configPath;
}

void EditorWindow::setStudioStyle(const StudioStyle &style) {
    StudioDocument doc = m_studio;
    doc.style = style;
    setStudioDocument(doc);
}

void EditorWindow::setStudioDocument(const StudioDocument &doc) {
    m_studio = doc;
    m_toolbar->setStudioActive(doc.style.active());
    if (zoomlane::indexOf(m_studio.zooms, m_selectedZoom) < 0) m_selectedZoom = 0;
    if (m_studio.fragments.isEmpty() || m_selectedFragment >= m_studio.fragments.size()) m_selectedFragment = -1;
    if (m_timeline) m_timeline->setSelectedFragment(m_selectedFragment);
    setCropRect(m_cropRect);   // the base view follows the ratio and "keep zoomed in"
    if (isVideo()) onVideoContentChanged();
}

void EditorWindow::editStudio(const std::function<void(StudioDocument &)> &change, int mergeKey) {
    StudioDocument after = m_studio;
    change(after);
    if (after == m_studio) return;
    m_undo->push(new SetStudioDocumentCommand(m_studio, after,
        [this](const StudioDocument &d) { setStudioDocument(d); }, mergeKey));
}

void EditorWindow::editZoom(quint32 id, const std::function<void(ZoomSegment &)> &change, int mergeKey) {
    editStudio([&](StudioDocument &d) {
        const int i = zoomlane::indexOf(d.zooms, id);
        if (i >= 0) change(d.zooms[i]);
    }, mergeKey);
}

void EditorWindow::removeZoom(quint32 id) {
    editStudio([id](StudioDocument &d) { d.zooms.removeIf([id](const ZoomSegment &z) { return z.id == id; }); });
}

QRect EditorWindow::cameraContent() const {
    return m_cropRect.isEmpty() ? QRect(QPoint(), m_media.nativeSize()) : m_cropRect;
}

QRect EditorWindow::cameraBase() const {
    const QRect content = cameraContent();
    return isVideo() && m_studio.keepZoomedIn
        ? keepZoomedInRect(content, m_studio.style, m_studio.keepCenter) : content;
}

void EditorWindow::updateStudioPreview() {
    if (!m_studio.style.active()) { m_canvas->clearStudioFrame(); return; }
    const QRect content = cameraBase();
    const StudioLayout layout = studioLayout(content.size(), m_studio.style);
    // Lengths are relative to the content, so a smaller render looks the same;
    // this keeps slider drags cheap on 4K media.
    const qreal scale = qMin(1.0, 1600.0 / qMax(1, qMax(content.width(), content.height())));
    const QSize previewSize = (QSizeF(content.size()) * scale).toSize().expandedTo(QSize(1, 1));
    const QPixmap background = QPixmap::fromImage(renderStudioBackground(previewSize, m_studio.style));
    m_canvas->setStudioFrame(background,
        QRectF(content.topLeft() - layout.content.topLeft(), layout.output), layout.radius);
}

void EditorWindow::openStudio() {
    // A second click on the button closes the popover instead of stacking one.
    if (m_studioPopover) { m_studioPopover->close(); return; }
    finishCrop();
    const StudioDocument before = m_studio;
    // Switching Studio on starts from the style used last time.
    if (!m_studio.style.active()) setStudioStyle(loadLastStudioStyle(configPath()));
    const QRect content = cameraContent();
    auto keepAvailable = [this, content](const StudioStyle &style) {
        return keepZoomedInRect(content, style, QRectF(content).center()) != content;
    };
    StudioCameraSettings camera;
    camera.available = isVideo();
    // The zooms' one shared motion; the default while there are none.
    camera.motion = m_studio.zooms.isEmpty() ? m_studio.motion : m_studio.zooms.first().motion;
    for (const ZoomSegment &z : std::as_const(m_studio.zooms))
        if (z.motion != m_studio.zooms.first().motion) camera.motion.reset();
    camera.keepZoomedIn = m_studio.keepZoomedIn;
    camera.keepZoomedInAvailable = keepAvailable(m_studio.style);
    camera.suggestAvailable = isVideo() && m_cursorTrack != nullptr;
    auto *popover = new StudioPopover(m_studio.style, cameraBase().size(), camera, this);
    popover->setAttribute(Qt::WA_DeleteOnClose);
    popover->setAttribute(Qt::WA_TranslucentBackground);
    m_studioPopover = popover;
    connect(popover, &StudioPopover::styleChanged, this, [this, popover, keepAvailable](const StudioStyle &style) {
        setStudioStyle(style);
        popover->setKeepZoomedInAvailable(keepAvailable(style));
        popover->setContentSize(cameraBase().size());
    });
    connect(popover, &StudioPopover::motionChosen, this, [this](ZoomSegment::Motion motion) {
        StudioDocument doc = m_studio;
        doc.motion = motion;
        for (ZoomSegment &z : doc.zooms) z.motion = motion;
        setStudioDocument(doc);
    });
    connect(popover, &StudioPopover::keepZoomedInChanged, this, [this, popover](bool on) {
        StudioDocument doc = m_studio;
        doc.keepZoomedIn = on;
        if (on && doc.keepCenter.isNull()) doc.keepCenter = QRectF(cameraContent()).center();
        setStudioDocument(doc);
        popover->setContentSize(cameraBase().size());
    });
    // Suggestions are ordinary zooms; with the rest of the session one undo step.
    connect(popover, &StudioPopover::suggestRequested, this, [this] {
        const QVector<ZoomSegment> added = suggestZooms(*m_cursorTrack, m_timeMap, m_media.video.durationMs,
            m_studio.zooms, zoomlane::nextId(m_studio.zooms), m_studio.motion);
        if (added.isEmpty()) {
            m_toast->showMessage(tr("No clicks or pauses to zoom on"));
            return;
        }
        StudioDocument doc = m_studio;
        for (const ZoomSegment &z : added) zoomlane::insert(doc.zooms, z);
        setStudioDocument(doc);
        m_toast->showMessage(added.size() == 1 ? tr("Added 1 zoom") : tr("Added %1 zooms").arg(added.size()));
    });
    connect(popover, &StudioPopover::imageRequested, this, [this, popover] {
        // The popover's deferred deletion runs after the dialog returns, so its
        // single undo step still includes the chosen image.
        popover->close();
        const QString path = QFileDialog::getOpenFileName(this, tr("Background image"), {},
            tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
        if (path.isEmpty()) return;
        StudioStyle style = m_studio.style;
        style.background = StudioStyle::Background::Image;
        style.imagePath = path;
        setStudioStyle(style);
    });
    connect(popover, &QObject::destroyed, this, [this, before] {
        if (m_studio == before) return;
        if (m_studio.style != before.style) saveLastStudioStyle(configPath(), m_studio.style);
        m_undo->push(new SetStudioDocumentCommand(before, m_studio,
            [this](const StudioDocument &d) { setStudioDocument(d); }));
    });
    auto *button = m_toolbar->findChild<QToolButton *>(QStringLiteral("Studio"));
    const QPoint anchor = button->mapToGlobal(QPoint(button->width(), button->height() + 4));
    popover->move(anchor - QPoint(popover->width(), 0));
    popover->show();
}

// The camera (studio plan 3.4 and 5): one curve for preview and export, rebuilt
// after every edit and read per shown frame.
void EditorWindow::rebuildCamera() {
    if (!isVideo() || !m_timeline || !m_cameraRebuild) return;
    m_cameraRebuild->stop();
    const QRect content = cameraContent(), base = cameraBase();
    // Simulating the springs costs time on long clips; style edits leave the path alone.
    const bool follows = m_studio.keepZoomedIn && m_studio.keepFollowsCursor;
    CameraInputs inputs{m_studio.zooms, m_studio.fragments, content, base,
                        m_trimInMs, m_trimOutMs, m_media.video.durationMs, follows};
    if (inputs != m_cameraInputs) {
        m_cameraInputs = inputs;
        m_timeMap = TimeMap(m_media.video.durationMs, m_trimInMs, m_trimOutMs, m_studio.fragments);
        m_cameraPath = CameraPath(m_studio.zooms, m_timeMap,
            CameraFrame{QRectF(content), base == content ? 0.0 : double(base.width()) / base.height(),
                        QRectF(base).center(), m_cursorTrack.get(), follows});
    }
    m_timeline->setFragments(m_studio.fragments);
    updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
    m_timeline->setZoomLaneVisible(m_studio.style.active() || !m_studio.zooms.isEmpty());
    m_timeline->setZooms(m_studio.zooms, [this](qint64 source) {
        const auto out = m_timeMap.toOutput(double(source));
        return out ? m_cameraPath.zoomAt(*out) : 1.0;
    });
    m_timeline->setSelectedZoom(m_selectedZoom);
    updateCamera();
    refreshZoomUi();
}

QRectF EditorWindow::currentCamera() const {
    if (m_showBaseView) return QRectF(cameraBase());
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const int selected = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
    // A selected zoom shows where it comes to rest, not a moment of the ride.
    if (!playing && selected >= 0) {
        const ZoomSegment &z = m_studio.zooms[selected];
        if (z.target != ZoomSegment::Target::Cursor || !m_cursorTrack) return m_cameraPath.targetRect(z);
        // A Cursor zoom rests around the pointer: at the playhead, or at the
        // zoom's start when the playhead is elsewhere.
        const qint64 at = m_timeline->position() >= z.startMs && m_timeline->position() < z.endMs
            ? m_timeline->position() : z.startMs;
        ZoomSegment resting = z;
        if (const auto pointer = m_cursorTrack->positionAt(at)) resting.point = *pointer;
        return m_cameraPath.targetRect(resting);
    }
    const qint64 source = playing && m_presentedStart >= 0 ? m_presentedStart : m_timeline->position();
    const double out = source <= m_trimInMs ? 0.0 : m_timeMap.toOutputAfter(double(source));
    return m_cameraPath.rectAt(out);
}

void EditorWindow::updateCamera() {
    if (!isVideo() || !m_timeline) return;
    const bool cropping = m_crop && m_crop->active();
    m_canvas->setCamera(cropping ? QRectF() : currentCamera());
    if (m_miniMap && m_miniMap->isVisible()) m_miniMap->setCamera(currentCamera());
}

void EditorWindow::selectZoom(quint32 id) {
    if (zoomlane::indexOf(m_studio.zooms, id) < 0) id = 0;
    m_selectedZoom = id;
    if (id && m_selectedFragment >= 0) {
        m_selectedFragment = -1;
        if (m_timeline) m_timeline->setSelectedFragment(-1);
    }
    if (m_timeline) m_timeline->setSelectedZoom(id);
    updateCamera();
    refreshZoomUi();
}

void EditorWindow::addZoomAt(qint64 sourceMs) {
    const auto span = zoomlane::placeNew(m_studio.zooms, sourceMs, m_media.video.durationMs);
    if (!span) {
        m_toast->showMessage(tr("No room for a zoom here"));
        return;
    }
    ZoomSegment z;
    z.id = zoomlane::nextId(m_studio.zooms);
    z.startMs = span->first;
    z.endMs = span->second;
    z.point = m_lastZoomPoint.isNull() ? QRectF(cameraBase()).center() : m_lastZoomPoint;
    z.motion = m_studio.motion;
    if (m_player) m_player->pause();
    editStudio([&](StudioDocument &d) { zoomlane::insert(d.zooms, z); });
    selectZoom(z.id);
}

void EditorWindow::selectFragment(int index) {
    if (m_studio.fragments.isEmpty() || index >= m_studio.fragments.size()) index = -1;
    if (index >= 0 && m_selectedZoom) {
        m_selectedZoom = 0;
        if (m_timeline) m_timeline->setSelectedZoom(0);
        updateCamera();
    }
    m_selectedFragment = index;
    if (m_timeline) m_timeline->setSelectedFragment(index);
    refreshZoomUi();
}

// S splits at the playhead and selects the part after it (studio plan 6.6).
void EditorWindow::splitAtPlayhead() {
    const qint64 at = m_timeline->position();
    QVector<Fragment> list = m_studio.fragments;
    if (!fragments::split(list, at, m_media.video.durationMs)) {
        m_toast->showMessage(tr("Too close to a split to split again"));
        return;
    }
    editStudio([&](StudioDocument &d) { d.fragments = list; });
    selectFragment(fragments::indexAt(list, at));
}

// Playback skips cuts and plays each fragment at its speed times the preview
// rate. Seeks at a seam can stutter a frame; the export is exact.
void EditorWindow::followPlaybackPieces(qint64 sourceMs) {
    if (!m_player || m_studio.fragments.isEmpty() || m_seekSettling) {
        if (m_player && m_studio.fragments.isEmpty() && !qFuzzyCompare(m_player->playbackRate(), m_previewRate))
            m_player->setPlaybackRate(m_previewRate);
        return;
    }
    const QVector<TimePiece> &pieces = m_timeMap.pieces();
    for (const TimePiece &piece : pieces) {
        if (sourceMs < piece.srcStart - 1) {   // in a cut: on to the next kept piece
            m_player->setPosition(qRound64(piece.srcStart) + 1);
            return;
        }
        if (sourceMs < piece.srcEnd) {
            const qreal rate = piece.speed * m_previewRate;
            if (!qFuzzyCompare(m_player->playbackRate(), rate)) m_player->setPlaybackRate(rate);
            return;
        }
    }
    // Past the last kept piece: the rest up to the trim's end is cut.
    if (sourceMs < m_trimOutMs) handlePlaybackEnd();
}

// Steers the selected zoom's target, or the "keep zoomed in" centre when no
// zoom is selected. The whole drag becomes one undo step when it ends.
void EditorWindow::moveCameraTarget(QPointF delta) {
    if (!m_cameraGesture) {
        m_cameraGesture = true;
        m_cameraGestureBefore = m_studio;
    }
    const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
    if (i >= 0) {
        ZoomSegment &z = m_studio.zooms[i];
        if (z.target == ZoomSegment::Target::Cursor && m_cursorTrack) return;   // the pointer leads
        z.point += delta;
        z.point = m_cameraPath.targetRect(z).center();   // no drift past the edges
        m_lastZoomPoint = z.point;
    } else if (m_studio.keepZoomedIn) {
        const QRectF content(cameraContent());
        const QSizeF view = QRectF(cameraBase()).size();
        const QPointF c = m_studio.keepCenter + delta;
        m_studio.keepFollowsCursor = false;   // placed by hand now (E10)
        m_studio.keepCenter = QPointF(
            qBound(content.left() + view.width() / 2, c.x(), content.right() - view.width() / 2),
            qBound(content.top() + view.height() / 2, c.y(), content.bottom() - view.height() / 2));
        m_showBaseView = true;
        applyContentRect();
    }
    // The selected zoom's resting view and the base view need no new path; the
    // path is rebuilt once when the drag ends.
    updateCamera();
    refreshZoomUi();
}

void EditorWindow::finishCameraGesture(bool cancelled) {
    if (!m_cameraGesture) return;
    m_cameraGesture = false;
    m_showBaseView = false;
    const StudioDocument before = m_cameraGestureBefore, after = m_studio;
    if (cancelled || before == after) {
        setStudioDocument(before);
        return;
    }
    m_undo->push(new SetStudioDocumentCommand(before, after,
        [this](const StudioDocument &d) { setStudioDocument(d); }));
}

void EditorWindow::refreshZoomUi() {
    if (!m_zoomBar) return;
    const bool cropping = m_crop && m_crop->active();
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
    const bool keep = cameraBase() != cameraContent();
    if (i >= 0 && !cropping) m_zoomBar->setZoom(m_studio.zooms[i]);
    m_zoomBar->setVisible(i >= 0 && !cropping);
    const bool fragment = m_selectedFragment >= 0 && m_selectedFragment < m_studio.fragments.size() && !cropping;
    if (fragment)
        m_fragmentBar->setFragment(m_studio.fragments[m_selectedFragment],
                                   m_selectedFragment > 0 && !m_studio.fragments[m_selectedFragment - 1].removed);
    m_fragmentBar->setVisible(fragment);
    const bool map = !cropping && !playing && (i >= 0 || keep);
    if (map && !m_miniMap->isVisible()) {
        // The picture only changes with the frame; see videoFrameChanged. m_bg is
        // the frame in document orientation and size, like the canvas shows it.
        const QRect content = cameraContent();
        m_miniMap->setContent(m_lastVideoFrame.isValid() && updateVideoBackground()
            ? m_bg.copy(content).scaled(304, 224, Qt::KeepAspectRatio, Qt::SmoothTransformation) : QImage(),
            QRectF(content));
    }
    if (map) m_miniMap->setCamera(currentCamera());
    m_miniMap->setVisible(map);
    m_canvas->setCameraDragEnabled(map);
    positionZoomUi();
}

void EditorWindow::positionZoomUi() {
    if (!m_zoomBar) return;
    const QSize viewport = m_canvas->viewport()->size();
    m_zoomBar->adjustSize();
    const QRect bar(QPoint((viewport.width() - m_zoomBar->width()) / 2,
                           viewport.height() - m_zoomBar->height() - 12), m_zoomBar->size());
    m_zoomBar->move(bar.topLeft());
    m_fragmentBar->adjustSize();
    m_fragmentBar->move((viewport.width() - m_fragmentBar->width()) / 2,
                        viewport.height() - m_fragmentBar->height() - 12);
    m_fragmentBar->raise();
    // Bottom right inside the content; above the bar when they would meet.
    const QRectF camera = m_canvas->camera().isEmpty() ? m_canvas->contentRect() : m_canvas->camera();
    const QRect content = QRect(m_canvas->mapFromScene(camera.topLeft()),
                                m_canvas->mapFromScene(camera.bottomRight()))
                              .intersected(QRect(QPoint(), viewport));
    m_miniMap->setWidthLimit(qBound(72, content.width() / 4, 152));
    QRect map(QPoint(content.right() - m_miniMap->width() - 12, content.bottom() - m_miniMap->height() - 12),
              m_miniMap->size());
    if (m_zoomBar->isVisible() && map.intersects(bar.adjusted(-6, -6, 6, 6))) map.moveBottom(bar.top() - 12);
    m_miniMap->move(map.topLeft());
    m_zoomBar->raise();
    m_miniMap->raise();
}

QImage EditorWindow::renderAnnotationOverlay() {
    const auto selection = m_scene->selectedItems();
    m_scene->clearSelection();          // drop selection handles so they aren't baked into the video
    QList<RedactItem *> visibleBlurItems;
    for (QGraphicsItem *item : m_scene->items()) {
        auto *redact = dynamic_cast<RedactItem *>(item);
        if (redact && RedactItem::isBlur(redact->mode()) && redact->isVisible()) {
            visibleBlurItems.append(redact);
            redact->hide();
        }
    }
    const bool hadBackground = m_backgroundItem != nullptr;
    const bool wasVisible = hadBackground && m_backgroundItem->isVisible();
    m_renderingVideoOverlay = true;
    const int renderGeneration = ++m_videoOverlayRenderGeneration;
    if (hadBackground) m_backgroundItem->setVisible(false);
    QImage overlay = renderToImage(*m_scene, m_media.nativeSize());
    if (hadBackground) m_backgroundItem->setVisible(wasVisible);
    for (RedactItem *redact : visibleBlurItems) redact->show();
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
    return hasVideoAnnotations() || hasTrim() || !m_cropRect.isEmpty() || m_studio.style.active()
        || !m_studio.zooms.isEmpty() || cameraBase() != cameraContent()
        || m_exportSettings != ExportSettings{} || !m_studio.fragments.isEmpty();
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
    rebuildCamera();
    if (m_player && !m_timelineActive) {
        m_player->pause();
        m_resumeAfterSeek = false;
        if (m_player->position() < m_trimInMs || m_player->position() >= m_trimOutMs) {
            requestVideoSeek(m_trimInMs);
            m_timeline->setPosition(m_trimInMs);
            flushVideoSeek();
        }
    }
}

void EditorWindow::updateTrimTimeLabels(qint64 inMs, qint64 outMs) {
    for (auto *field : {m_trimInLabel, m_trimOutLabel}) {
        if (!field) continue;
        if (!field->hasFocus() || !field->isModified())
            field->setText(formatPreciseTime(field == m_trimInLabel ? inMs : outMs));
        field->setFixedWidth(field->fontMetrics().horizontalAdvance(formatPreciseTime(m_media.video.durationMs)) + 16);
    }
    // The output's length: cuts left out, sped-up fragments shorter.
    if (m_trimDurationLabel)
        m_trimDurationLabel->setText(formatPreciseTime(qRound64(
            TimeMap(m_media.video.durationMs, inMs, outMs, m_studio.fragments).outputDurationMs())));
}

void EditorWindow::commitTrimTime(QLineEdit *field) {
    if (!field->isModified()) return;
    qint64 time = 0;
    const qint64 gap = m_media.video.fps > 0 ? qMax<qint64>(1, qRound64(1000 / m_media.video.fps)) : 1;
    const bool valid = parseVideoTime(field->text(), &time)
        && (field == m_trimInLabel ? time >= 0 && time <= m_trimOutMs - gap
                                  : time >= m_trimInMs + gap && time <= m_media.video.durationMs);
    field->setModified(false);
    if (valid) applyTrimRange(field == m_trimInLabel ? time : m_trimInMs,
                              field == m_trimOutLabel ? time : m_trimOutMs);
    else if (m_toast) m_toast->showMessage(tr("Enter a time inside the clip, with Start before End"));
    updateTrimTimeLabels(m_trimInMs, m_trimOutMs);
}

void EditorWindow::requestVideoSeek(qint64 position) {
    ensureVideoPlayer();
    m_seekTarget = qBound<qint64>(0, position, qMax<qint64>(0, m_media.video.durationMs - 1));
    m_seekSettling = true;
    m_seekSettleTimer->start();
    if (m_timeLabel) m_timeLabel->setText(formatPreciseTime(m_seekTarget)
        + QStringLiteral(" / ") + formatTime(m_media.video.durationMs));
    if (!m_seekTimer->isActive()) m_seekTimer->start();
}

void EditorWindow::flushVideoSeek() {
    m_seekTimer->stop();
    if (!m_player || m_seekTarget < 0 || !m_player->isSeekable()) return;
    m_seekSettling = true;
    m_seekSettleTimer->start();
    if (qAbs(m_player->position() - m_seekTarget) <= 1 && m_presentedStart >= 0
        && m_seekTarget >= m_presentedStart && m_seekTarget < m_presentedEnd) {
        finishVideoSeek();
        return;
    }
    // Some backends return the frame ending exactly at the requested time.
    // Seek just inside the requested millisecond so a boundary selects its frame.
    m_hasSentVideoSeek = true;
    m_player->setPosition(qMin(m_seekTarget + 1, m_media.video.durationMs - 1));
}

void EditorWindow::finishVideoSeek() {
    m_seekSettling = false;
    m_seekSettleTimer->stop();
    m_loopSeeking = false;
    if (!m_timelineActive) {
        if (m_copyFramePending) {
            m_copyFramePending = false;
            copyVideoFrame();
        }
        if (m_resumeAfterSeek && m_seekTarget >= m_trimInMs && m_seekTarget < m_trimOutMs)
            m_player->play();
        m_resumeAfterSeek = false;
    }
}

QString EditorWindow::videoDeliveryPath() {
    finishCrop();
    if (!hasVideoEdits())
        return m_media.path;
    if (m_cachedVideoRevision == m_videoRevision && QFileInfo::exists(m_cachedVideoPath))
        return m_cachedVideoPath;
    m_videoStatusRequested = true;
    scheduleVideoExportCache(0);
    if (m_toast)
        m_toast->showMessage(QStringLiteral("Preparing video export…"));
    return {};
}

void EditorWindow::onVideoContentChanged() {
    if (!isVideo()) return;
    // Undo/redo may restore a redaction while paused on a newer source frame.
    for (QGraphicsItem *item : m_scene->items()) {
        if (dynamic_cast<RedactItem *>(item)) { updateVideoBackground(); break; }
    }
    ++m_videoRevision;
    const bool edited = hasVideoEdits();
    if (m_exportStatus) {
        m_exportStatus->setText(edited ? tr("Edited") : QString());
        m_exportStatus->setToolTip(tr("Video export status"));
    }
    if (m_dragPill) {
        m_dragPill->setPreparationNeeded(edited);
        m_dragPill->setEnabled(!m_videoExportInProgress || !edited);
    }
    if (edited) {
        scheduleVideoExportCache();
    } else if (m_videoStatusRequested) {
        completePendingVideoActions(m_media.path, false);
    }
}

void EditorWindow::scheduleVideoExportCache(int delayMs) {
    if (!isVideo() || !hasVideoEdits() || !m_videoExportTimer || m_timelineActive || !m_videoStatusRequested) return;
    m_videoExportTimer->start(qMax(0, delayMs));
}

QString EditorWindow::createVideoTempPath() const {
    QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/eddy-video-XXXXXX.") + outputSuffix());
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
    if (!isVideo() || !hasVideoEdits() || !m_videoStatusRequested || m_timelineActive
        || (m_crop && m_crop->active())) return;
    if (m_videoExportInProgress) {
        m_videoExportPending = true;
        return;
    }

    const QString path = createVideoTempPath();
    if (path.isEmpty()) {
        if (m_exportStatus) m_exportStatus->setText(tr("Export failed"));
        if (m_videoStatusRequested) failPendingVideoActions();
        return;
    }

    const int revision = m_videoRevision;
    VideoExportRequest request{
        m_media.path, path, renderAnnotationOverlay(),
        m_trimInMs, hasTrim() ? m_trimOutMs : -1, 30 * 60 * 1000
    };
    request.cropRect = m_cropRect;
    request.studio = m_studio.style;
    request.zooms = m_studio.zooms;
    request.fragments = m_studio.fragments;
    request.maxShortSide = m_exportSettings.shortSide;
    request.maxFps = m_exportSettings.fps;
    if (cameraBase() != cameraContent()) request.baseView = cameraBase();
    request.cursorTrack = m_cursorTrack;
    request.baseFollowsCursor = m_studio.keepZoomedIn && m_studio.keepFollowsCursor;
    m_videoExportCancel = std::make_shared<std::atomic_bool>(false);
    request.cancelled = [cancel = m_videoExportCancel] { return cancel->load(); };
    QPointer<EditorWindow> receiver(this);
    request.progress = [receiver, revision](int percent) {
        QMetaObject::invokeMethod(qApp, [receiver, revision, percent] {
            if (receiver && receiver->m_videoExportInProgress && receiver->m_videoExportCancel
                && !receiver->m_videoExportCancel->load()
                && receiver->m_videoRevision == revision && receiver->m_exportStatus) {
                receiver->m_exportStatus->setText(percent < 0 ? tr("Exporting…")
                    : tr("Exporting %1%").arg(percent));
            }
        }, Qt::QueuedConnection);
    };
    for (QGraphicsItem *item : m_scene->items())
        if (auto *redact = dynamic_cast<RedactItem *>(item))
            request.blurRects += redact->blurRectsInScene();
    m_videoExportInProgress = true;
    if (m_dragPill) m_dragPill->setEnabled(false);
    if (m_exportStatus) {
        m_exportStatus->setText(tr("Preparing…"));
        m_exportStatus->setToolTip(tr("Video export status"));
    }
    if (m_exportCancel) m_exportCancel->show();

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
    const bool cancelled = m_videoExportCancel && m_videoExportCancel->load();
    m_videoExportCancel.reset();
    if (m_exportCancel) m_exportCancel->hide();
    const bool current = result.ok && hasVideoEdits() && revision == m_videoRevision;
    if (current) {
        if (!m_cachedVideoPath.isEmpty() && m_cachedVideoPath != path
            && !m_clipboardVideoPaths.contains(m_cachedVideoPath)
            && !m_videoIpcPaths.contains(m_cachedVideoPath)) {
            QFile::remove(m_cachedVideoPath);
        }
        m_cachedVideoPath = path;
        m_cachedVideoRevision = revision;
        if (m_dragPill) {
            m_dragPill->setPreparationNeeded(false);
            m_dragPill->setEnabled(true);
        }
        if (m_exportStatus) m_exportStatus->setText(tr("Ready"));
    } else {
        QFile::remove(path);
        if (cancelled) {
            if (m_exportStatus) m_exportStatus->setText(hasVideoEdits() ? tr("Edited") : QString());
        } else if (!result.ok && revision == m_videoRevision) {
            if (m_exportStatus) {
                m_exportStatus->setText(tr("Export failed"));
                m_exportStatus->setToolTip(result.error);
            }
            std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
        }
    }

    if (revision == m_videoRevision && m_videoStatusRequested) {
        if (result.ok)
            completePendingVideoActions(m_cachedVideoPath, true);
        else
            failPendingVideoActions(result.error);
    }

    const bool needsFreshExport = hasVideoEdits() && revision != m_videoRevision;
    if (m_videoStatusRequested && (m_videoExportPending || needsFreshExport)) {
        m_videoExportPending = false;
        scheduleVideoExportCache(100);
    } else if (m_closeAfterVideoExport) {
        m_closeAfterVideoExport = false;
        close();
    }
}

void EditorWindow::cancelVideoExport() {
    if (!m_videoExportInProgress || !m_videoExportCancel) return;
    // ffmpeg is killed on the export thread; finishVideoExportCache cleans up.
    m_videoExportCancel->store(true);
    if (m_videoExportTimer) m_videoExportTimer->stop();
    m_videoExportPending = false;
    failPendingVideoActions();
    if (m_exportCancel) m_exportCancel->hide();
    if (m_exportStatus) m_exportStatus->setText(tr("Cancelling…"));
    if (m_toast) m_toast->showMessage(tr("Video export cancelled"));
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

void EditorWindow::failPendingVideoActions(const QString &reason) {
    m_videoStatusRequested = false;
    if (m_dragPill) m_dragPill->setEnabled(true);
    m_copyVideoPending = false;
    m_sendVideoToShelfPending = false;
    m_videoShelfFallbackPending = false;
    m_replaceVideoCardPending = false;
    m_videoSavePendingPath.clear();
    m_videoSavePendingCopy = false;
    m_videoSavePendingClose = false;
    m_closeAfterVideoShelf = false;
    m_closeAfterVideoCard = false;
    // ffmpeg reports the actual cause on its last stderr line.
    const QString detail = reason.trimmed().section(QLatin1Char('\n'), -1).trimmed();
    if (m_toast)
        m_toast->showMessage(detail.isEmpty() ? tr("Video export failed")
            : tr("Video export failed: %1").arg(fontMetrics().elidedText(detail, Qt::ElideRight, 480)),
            6000);
}

void EditorWindow::completePendingVideoActions(const QString &path, bool takeOwnership) {
    const bool requested = m_videoStatusRequested;
    m_videoStatusRequested = false;

    bool copyPending = m_copyVideoPending;
    m_copyVideoPending = false;
    const bool shelfPending = m_sendVideoToShelfPending;
    m_sendVideoToShelfPending = false;
    const bool shelfFallback = m_videoShelfFallbackPending;
    m_videoShelfFallbackPending = false;
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
        postVideoToShelf(path, takeOwnership, copyPending, shelfFallback);
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
        m_videoShelfFallbackPending = true;   // no shelf: the clipboard still gets the video
        sendToShelf();
        return;
    }
    if (route == SaveRoute::BoltsnapCard && !shelfTakes()) {
        m_toast->showMessage(tr("The shelf takes MP4 only, copied instead"));
        copy();
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
        const QString suffix = exportSuffix(m_exportSettings, m_media.path);
        path = QDir(m_cli.output.saveDir).filePath(
            QStringLiteral("eddy-")
            + QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss")
            + QStringLiteral(".") + suffix);
    } else if (route == SaveRoute::ConfigDirectory) {
        const QString suffix = exportSuffix(m_exportSettings, m_media.path);
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
    if (isProjectPath(path)) {   // a project's original and manifest are never an output
        m_toast->showMessage(tr("Cannot save over a project file"));
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

void EditorWindow::postVideoToShelf(const QString &path, bool takeOwnership, bool copyAfter,
                                    bool fallbackOnFailure) {
    const QString output = screen() ? screen()->name() : QString();
    const bool closeAfter = m_closeAfterVideoShelf;
    const QString pinnedPath = path == m_cachedVideoPath ? path : QString();
    m_closeAfterVideoShelf = false;
    runVideoIpc(
        [path, takeOwnership, output] {
            return sendVideoToBoltsnapShelf(
                path, QStringLiteral("eddy"), takeOwnership, output);
        },
        [this, path, copyAfter, fallbackOnFailure, closeAfter](const DeliverResult &result) {
            if (!result.ok) {
                std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
                if (m_toast)
                    m_toast->showMessage(QStringLiteral("Boltsnap shelf unavailable"));
                if (copyAfter || fallbackOnFailure) copyVideoFile(path);
                return;
            }
            if (copyAfter) copyVideoFile(result.path);
            if (m_toast) m_toast->showMessage(QStringLiteral("Sent to Boltsnap shelf"));
            if (closeAfter) close();
        }, pinnedPath);
}

void EditorWindow::save() {
    finishCrop();
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

    if (!path.isEmpty() && isProjectPath(path)) {
        m_toast->showMessage(tr("Cannot save over a project file"));
        return;
    }
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

// An explicit -o path names its own format; everything else follows the
// export popover.
QString EditorWindow::outputSuffix() const {
    if (m_cli.output.toFile && !QFileInfo(m_cli.output.filePath).suffix().isEmpty())
        return QFileInfo(m_cli.output.filePath).suffix().toLower();
    return exportSuffix(m_exportSettings, m_media.path);
}

// Boltsnap's shelf offers video cards as MP4 only (studio plan 6.8).
bool EditorWindow::shelfTakes() const {
    return outputSuffix() == QLatin1String("mp4")
        || m_exportSettings.format == ExportSettings::Format::Original;
}

void EditorWindow::sendToShelf() {
    finishCrop();
    if (isVideo() && !shelfTakes()) {
        m_videoShelfFallbackPending = false;
        m_closeAfterVideoShelf = false;
        m_toast->showMessage(tr("The shelf takes MP4 only, copied instead"));
        copy();
        return;
    }
    if (isVideo()) {
        if (!hasVideoEdits()) {
            const bool copyAfter = m_copyVideoPending;
            const bool fallbackOnFailure = m_videoShelfFallbackPending;
            m_copyVideoPending = false;
            m_videoShelfFallbackPending = false;
            postVideoToShelf(m_media.path, false, copyAfter, fallbackOnFailure);
            return;
        }
        if (m_cachedVideoRevision == m_videoRevision && QFileInfo::exists(m_cachedVideoPath)) {
            const bool copyAfter = m_copyVideoPending;
            const bool fallbackOnFailure = m_videoShelfFallbackPending;
            m_copyVideoPending = false;
            m_videoShelfFallbackPending = false;
            postVideoToShelf(m_cachedVideoPath, true, copyAfter, fallbackOnFailure);
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

void EditorWindow::openExportPanel() {
    const QSize framed = studioLayout(cameraBase().size(), m_studio.style).output;
    m_exportPanel->setOutput(framed, isVideo() ? qint64(m_timeMap.outputDurationMs()) : 0, m_media.path);
    m_exportPanel->setSettings(m_exportSettings);
}

ProjectSnapshot EditorWindow::projectSnapshot() const {
    ProjectSnapshot p;
    p.kind = m_media.kind;
    p.sourceName = m_projectSourceName.isEmpty() ? QFileInfo(m_media.path).fileName() : m_projectSourceName;
    p.size = m_media.nativeSize();
    p.crop = m_cropRect;
    if (isVideo()) {
        p.durationMs = m_media.video.durationMs;
        p.trimInMs = m_trimInMs;
        p.trimOutMs = m_trimOutMs;
        p.positionMs = m_timeline ? m_timeline->position() : 0;
        p.exportSettings = m_exportSettings;
    }
    p.studio = m_studio;
    QList<QGraphicsItem *> items;
    for (QGraphicsItem *item : m_scene->items(Qt::AscendingOrder))
        if (!item->parentItem() && item->zValue() > -1000) items.append(item);
    p.items = itemsToJson(items);
    return p;
}

bool EditorWindow::applyProject(const ProjectSnapshot &project, const QString &manifestPath, QString *error) {
    if (project.size != m_media.nativeSize() || project.kind != m_media.kind) {
        if (error) *error = QStringLiteral("the project's original does not match its record");
        return false;
    }
    QString itemError;
    const auto items = itemsFromJson(project.items, m_bg, m_media.nativeSize(), &itemError);
    if (!items) {
        if (error) *error = itemError;
        return false;
    }
    m_restoring = true;
    const auto restored = qScopeGuard([this] { m_restoring = false; });
    setCropRect(project.crop);
    if (isVideo()) {
        m_exportSettings = project.exportSettings;
        setTrimRangeState(project.trimInMs, project.trimOutMs > 0 ? project.trimOutMs : m_media.video.durationMs);
        if (project.positionMs > 0) {
            m_timeline->setPosition(project.positionMs);
            requestVideoSeek(project.positionMs);
        }
    }
    setStudioDocument(project.studio);
    for (QGraphicsItem *item : *items) m_scene->addItem(item);
    m_undo->clear();   // a reopened project starts without history (21.09. plan 5)
    m_projectPath = manifestPath;
    m_projectAssetInfo = {true, {}, project.asset, project.sha256, project.assetSize};
    m_projectSourceName = project.sourceName;
    if (isVideo()) onVideoContentChanged();
    return true;
}

void EditorWindow::saveProject(bool saveAs, const QString &path) {
    if (m_projectSaving) return;
    m_tools->commitTextEdit();
    finishCrop();
    QString target = path;
    if (target.isEmpty() && (saveAs || m_projectPath.isEmpty())) {
        const QString name = QFileInfo(m_media.path.isEmpty() ? QStringLiteral("image") : m_media.path).completeBaseName();
        target = QFileDialog::getSaveFileName(this, tr("Save project with the original and its layers"),
            QDir(QFileInfo(m_media.path).absolutePath()).filePath(name + QStringLiteral(".eddy")),
            tr("Eddy project (*.eddy)"));
        if (target.isEmpty()) return;
        if (!target.endsWith(QLatin1String(".eddy"), Qt::CaseInsensitive)) target += QStringLiteral(".eddy");
    } else if (target.isEmpty()) {
        target = m_projectPath;
    }
    const bool reuse = target == m_projectPath;
    m_projectSaving = true;
    m_toast->showMessage(tr("Saving project…"));
    writeSnapshot(target, reuse ? m_projectAssetInfo : AssetResult(), true,
                  [this, target](const AssetResult &asset, const QString &error) {
        m_projectSaving = false;
        if (!error.isEmpty()) {
            std::fprintf(stderr, "eddy: %s\n", qPrintable(error));
            m_toast->showMessage(tr("Project not saved"));
            return;
        }
        m_projectPath = target;
        m_projectAssetInfo = asset;
        m_toast->showMessage(tr("Project saved with the original"));
        emit projectSaved(target);
    });
}

// Writes the document as a project at `target` in the background. `known` is
// the original already in target's assets folder, reused when it is still
// there; otherwise the source is copied once. `done` runs on this window.
void EditorWindow::writeSnapshot(const QString &target, const AssetResult &known, bool progress,
                                 std::function<void(const AssetResult &, const QString &)> done) {
    const ProjectSnapshot snapshot = projectSnapshot();
    const QString assets = projectAssetsDir(target);
    const QString source = m_media.path;
    const QImage image = m_media.path.isEmpty() ? m_media.image : QImage();
    const bool reuse = !known.name.isEmpty()
        && QFileInfo(QDir(assets).filePath(known.name)).size() == known.size;
    QPointer<EditorWindow> receiver(this);
    auto *thread = QThread::create([receiver, target, snapshot, assets, source, image, reuse, known, progress, done] {
        AssetResult asset = reuse ? known : image.isNull()
            ? storeAsset(source, assets, [receiver, progress](int percent) {
                  if (!progress || percent % 20) return;
                  QMetaObject::invokeMethod(qApp, [receiver, percent] {
                      if (receiver && receiver->m_projectSaving)
                          receiver->m_toast->showMessage(tr("Saving project %1%").arg(percent));
                  }, Qt::QueuedConnection);
              })
            : storeImageAsset(image, assets);
        ProjectSnapshot project = snapshot;
        DeliverResult written;
        if (asset.ok) {
            project.asset = asset.name;
            project.sha256 = asset.sha256;
            project.assetSize = asset.size;
            written = writeProject(target, project);
        }
        const QString error = !asset.ok ? asset.error : written.ok ? QString() : written.error;
        QMetaObject::invokeMethod(qApp, [receiver, asset, error, done] {
            if (receiver) done(asset, error);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// Kept edits (21.09. plan 5): a snapshot 2 s after the last finished change,
// at the latest 10 s after the first one, never in the middle of a gesture.
void EditorWindow::noteRecoveryChange() {
    if (m_recoveryPaused || m_restoring) return;
    m_recoveryPending = true;
    m_recoveryIdle->start();
    if (!m_recoveryMax->isActive()) m_recoveryMax->start();
}

bool EditorWindow::gestureRunning() const {
    return m_timelineActive || (m_timeline && m_timeline->zoomDragging()) || m_cameraGesture
        || m_canvas->cameraDragging() || m_tools->editingText() || (m_crop && m_crop->active());
}

void EditorWindow::writeRecovery() {
    if (!m_recoveryPending || m_recoveryPaused) return;
    if (gestureRunning() || m_recoveryWriting) {
        m_recoveryIdle->start(1000);
        return;
    }
    m_recoveryIdle->stop();
    m_recoveryMax->stop();
    const RecoveryStore store;
    if (m_recoveryId.isEmpty()) {
        // The first snapshot copies the original; keep within the budget.
        const qint64 size = m_media.path.isEmpty() ? qint64(m_media.image.sizeInBytes()) : QFileInfo(m_media.path).size();
        if (store.usedBytes() + size > RecoveryStore::kBudgetBytes) {
            m_recoveryPaused = true;
            m_toast->showMessage(tr("Kept edits are full (2 GB), this one is not kept"), 4000);
            return;
        }
        m_recoveryId = store.create();
        if (m_recoveryId.isEmpty()) {
            m_recoveryPaused = true;
            return;
        }
        m_recoveryLock = std::make_unique<QLockFile>(store.lockFileFor(m_recoveryId));
        m_recoveryLock->setStaleLockTime(0);
        m_recoveryLock->tryLock(0);
    }
    m_recoveryPending = false;
    m_recoveryWriting = true;
    const QString manifest = store.manifestFor(m_recoveryId);
    const QString id = m_recoveryId;
    const QString source = m_projectSource.isEmpty() ? m_media.path : m_projectSource;
    writeSnapshot(manifest, m_recoveryAsset, false, [this, id, source](const AssetResult &asset, const QString &error) {
        m_recoveryWriting = false;
        if (!error.isEmpty()) {
            std::fprintf(stderr, "eddy: cannot keep this edit: %s\n", qPrintable(error));
            m_toast->showMessage(tr("This edit could not be kept"));
            return;
        }
        m_recoveryAsset = asset;
        RecoveryStore().touch(id, source, QFileInfo(source.isEmpty() ? QStringLiteral("image.png") : source).fileName());
        emit recoveryWritten(RecoveryStore().manifestFor(id));
        if (m_recoveryPending) m_recoveryIdle->start();
    });
}

void EditorWindow::adoptRecovery(const QString &id) {
    const RecoveryStore store;
    m_recoveryId = id;
    m_recoveryLock = std::make_unique<QLockFile>(store.lockFileFor(id));
    m_recoveryLock->setStaleLockTime(0);
    m_recoveryLock->tryLock(0);
    // It continues as the same unnamed edit, not as a named project.
    m_recoveryAsset = m_projectAssetInfo;
    m_projectPath.clear();
    m_projectAssetInfo = {};
    for (const auto &entry : store.entries())
        if (entry.id == id) m_projectSource = entry.source;
}

// On close the last change is kept right away, within reason: the manifest
// always, a first copy of the original only when it is small.
void EditorWindow::flushRecovery() {
    if (!m_recoveryEnabled || !m_recoveryPending || m_recoveryPaused || m_recoveryWriting) return;
    m_recoveryIdle->stop();
    m_recoveryMax->stop();
    const RecoveryStore store;
    const QString source = m_projectSource.isEmpty() ? m_media.path : m_projectSource;
    AssetResult asset = m_recoveryAsset;
    if (m_recoveryId.isEmpty()) {
        constexpr qint64 kQuickCopy = 100ll * 1024 * 1024;
        if (!m_media.path.isEmpty() && QFileInfo(m_media.path).size() > kQuickCopy) return;
        if (store.usedBytes() > RecoveryStore::kBudgetBytes) return;
        m_recoveryId = store.create();
        if (m_recoveryId.isEmpty()) return;
        const QString assets = projectAssetsDir(store.manifestFor(m_recoveryId));
        asset = m_media.path.isEmpty() ? storeImageAsset(m_media.image, assets) : storeAsset(m_media.path, assets);
        if (!asset.ok) return;
    }
    ProjectSnapshot project = projectSnapshot();
    project.asset = asset.name;
    project.sha256 = asset.sha256;
    project.assetSize = asset.size;
    if (writeProject(store.manifestFor(m_recoveryId), project).ok) {
        m_recoveryPending = false;
        store.touch(m_recoveryId, source, QFileInfo(source.isEmpty() ? QStringLiteral("image.png") : source).fileName());
    }
}

// Reopening a file that has a newer kept edit offers it, without replacing
// the new edit on its own.
void EditorWindow::offerKeptEdit() {
    if (!m_recoveryEnabled || m_media.path.isEmpty() || !m_projectPath.isEmpty() || !m_recoveryId.isEmpty()) return;
    const QDateTime changed = QFileInfo(m_media.path).lastModified();
    for (const RecoveryStore::Entry &entry : RecoveryStore().entries()) {
        if (entry.inUse || entry.source != m_media.path || entry.updated < changed) continue;
        const QString id = entry.id, manifest = entry.manifest;
        m_toast->showAction(tr("A kept edit of this file is newer"), tr("Resume"), [this, id, manifest] {
            QString error;
            EditorWindow *window = openProjectWindow(manifest, m_cfg, m_cli, &error);
            if (!window) {
                std::fprintf(stderr, "eddy: %s\n", qPrintable(error));
                return;
            }
            window->adoptRecovery(id);
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
            if (m_undo->count() == 0) close();   // nothing here to lose
        }, 8000);
        return;
    }
}

void EditorWindow::openResumeDialog() {
    ResumeDialog dialog(RecoveryStore(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const RecoveryStore::Entry entry = dialog.chosen();
    QString error;
    EditorWindow *window = openProjectWindow(entry.manifest, m_cfg, m_cli, &error);
    if (!window) {
        m_toast->showMessage(tr("Cannot resume this edit"));
        std::fprintf(stderr, "eddy: %s\n", qPrintable(error));
        return;
    }
    window->adoptRecovery(entry.id);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

QString EditorWindow::recoveryManifest() const {
    return m_recoveryId.isEmpty() ? QString() : RecoveryStore().manifestFor(m_recoveryId);
}

void EditorWindow::setRecoveryDelays(int idleMs, int maxMs) {
    m_recoveryIdle->setInterval(idleMs);
    m_recoveryMax->setInterval(maxMs);
}

void EditorWindow::openProjectDialog() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open project"),
        m_projectPath.isEmpty() ? QString() : QFileInfo(m_projectPath).absolutePath(), tr("Eddy project (*.eddy)"));
    if (path.isEmpty()) return;
    QString error;
    EditorWindow *window = openProjectWindow(path, m_cfg, m_cli, &error);
    if (!window) {
        m_toast->showMessage(tr("Cannot open the project"));
        std::fprintf(stderr, "eddy: %s\n", qPrintable(error));
        return;
    }
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
}

EditorWindow *openProjectWindow(const QString &manifestPath, const Config &cfg, const CliOptions &cli,
                                QString *error) {
    OpenedProject opened = openProject(manifestPath);
    // A missing or changed original can be found again, checked byte for byte.
    if (!opened.ok && opened.originalMissing && qobject_cast<QApplication *>(QCoreApplication::instance())
        && QGuiApplication::platformName() != QLatin1String("offscreen")) {
        const QString candidate = QFileDialog::getOpenFileName(nullptr,
            QCoreApplication::translate("eddy", "Locate the original of %1").arg(QFileInfo(manifestPath).fileName()));
        if (!candidate.isEmpty()) {
            const DeliverResult relinked = relinkProjectSource(manifestPath, candidate);
            opened = relinked.ok ? openProject(manifestPath) : OpenedProject{false, relinked.error, {}, {}};
        }
    }
    if (!opened.ok) {
        if (error) *error = opened.error;
        return nullptr;
    }
    const LoadMediaResult media = loadMediaInput({InputSpec::File, opened.sourcePath});
    if (!media.ok) {
        if (error) *error = media.error;
        return nullptr;
    }
    // Output routes come from this session, never from the project (21.09. plan 5).
    CliOptions session = cli;
    session.boltsnapCardId = 0;
    auto *window = new EditorWindow(media.document, cfg, session);
    if (!window->applyProject(opened.snapshot, manifestPath, error)) {
        delete window;
        return nullptr;
    }
    return window;
}

void EditorWindow::copy() {
    finishCrop();
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

void EditorWindow::copyVideoFrame() {
    finishCrop();
    if (!isVideo()) return;
    if (m_seekSettling || m_timelineActive || m_seekTimer->isActive()) {
        m_copyFramePending = true;
        return;
    }
    if (!m_hasVideoFrame || !updateVideoBackground()) {
        m_toast->showMessage(tr("Frame unavailable"));
        return;
    }
    // Freeze the displayed source and render retained items, including blur.
    // Block scene signals so temporary focus/selection changes cannot commit text.
    const QSignalBlocker blocker(m_scene);
    const auto selection = m_scene->selectedItems();
    QGraphicsItem *focus = m_scene->focusItem();
    const bool focused = m_scene->hasFocus();
    const bool backgroundVisible = m_backgroundItem->isVisible();
    QGraphicsPixmapItem frozen(QPixmap::fromImage(m_bg));
    frozen.setZValue(-1000);
    m_scene->addItem(&frozen);
    const auto restore = qScopeGuard([&] {
        m_backgroundItem->setVisible(backgroundVisible);
        for (auto *item : selection) item->setSelected(true);
        m_scene->setFocusItem(focus);
        if (focused) m_scene->setFocus();
        m_handles->setVisible(true);
    });
    m_backgroundItem->hide();
    m_handles->setVisible(false);
    m_scene->clearSelection();
    m_scene->clearFocus();
    const QImage image = renderToImage(*m_scene, m_media.nativeSize());
    // What the canvas shows: the camera's window at the base view's size.
    const QRect base = cameraBase();
    const QRectF view = m_canvas->camera().isEmpty() ? QRectF(base) : m_canvas->camera();
    const QImage visible = image.copy(view.toAlignedRect())
        .scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QApplication::clipboard()->setImage(renderStudioImage(visible, m_studio.style));
    m_toast->showMessage(tr("Frame copied"));
}

void EditorWindow::keyPressEvent(QKeyEvent *e) {
    if (m_crop->active() && !e->modifiers().testFlag(Qt::ControlModifier)) {
        if (e->key() == Qt::Key_Escape) {
            if (m_crop->dragging()) m_crop->cancelDrag(); else m_crop->cancel();
            e->accept(); return;
        }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            m_crop->accept(); e->accept(); return;
        }
        const int step = e->modifiers().testFlag(Qt::ShiftModifier) ? 10 : (isVideo() ? 2 : 1);
        QPoint delta;
        if (e->key() == Qt::Key_Left) delta.setX(-step);
        if (e->key() == Qt::Key_Right) delta.setX(step);
        if (e->key() == Qt::Key_Up) delta.setY(-step);
        if (e->key() == Qt::Key_Down) delta.setY(step);
        if (!delta.isNull()) { m_crop->nudge(delta); e->accept(); return; }
    }
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
            m_tools->commitTextEdit();
            m_scene->clearSelection();
            m_scene->clearFocus();
            m_canvas->setFocus(Qt::OtherFocusReason);
            e->accept(); return;
        }
        QWidget::keyPressEvent(e);
        return;
    }
    if (isVideo() && m_timeline && e->modifiers() == Qt::NoModifier && e->key() == Qt::Key_S) {
        splitAtPlayhead();
        e->accept();
        return;
    }
    if (m_selectedFragment >= 0 && !m_selectedZoom && m_scene->selectedItems().isEmpty()
        && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
        if (!m_studio.fragments[m_selectedFragment].removed) emit m_fragmentBar->cutToggled();
        e->accept();
        return;
    }
    if (isVideo() && m_timeline && e->modifiers() == Qt::NoModifier && e->key() == Qt::Key_Z) {
        addZoomAt(m_timeline->position());
        e->accept();
        return;
    }
    // A selected zoom takes Delete and the arrows while no annotation is selected.
    if (const int i = zoomlane::indexOf(m_studio.zooms, m_selectedZoom);
        i >= 0 && m_scene->selectedItems().isEmpty()) {
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
            removeZoom(m_selectedZoom);
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
            const double fps = m_media.video.fps > 0 ? m_media.video.fps : 30.0;
            const int frames = (e->key() == Qt::Key_Left ? -1 : 1)
                * (e->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1);
            const qint64 start = m_studio.zooms[i].startMs + qRound64(frames * 1000.0 / fps);
            editStudio([this, start](StudioDocument &d) {
                zoomlane::move(d.zooms, m_selectedZoom, start, m_media.video.durationMs);
            }, 0x10000 + int(m_selectedZoom));
            e->accept();
            return;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        if (m_canvas->cameraDragging()) { m_canvas->cancelCameraDrag(); e->accept(); return; }
        if (m_timeline && m_timeline->zoomDragging()) { m_timeline->cancelInteraction(); e->accept(); return; }
        if (m_selectedZoom) { selectZoom(0); e->accept(); return; }
        if (m_selectedFragment >= 0) { selectFragment(-1); e->accept(); return; }
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
        case Qt::Key_J:
        case Qt::Key_L:
            if (isVideo() && m_timeline) {
                ensureVideoPlayer();
                if (m_player) m_player->pause();
                m_resumeAfterSeek = false;
                const qreal fps = m_media.video.fps > 0 ? m_media.video.fps : 30.0;
                const qint64 frame = qRound64(m_timeline->position() * fps / 1000.0)
                    + (e->key() == Qt::Key_J ? -1 : 1);
                const qint64 position = qBound<qint64>(0, qRound64(frame * 1000.0 / fps),
                    qMax<qint64>(0, m_timeline->duration() - 1));
                m_timeline->setPosition(position);
                requestVideoSeek(position);
                flushVideoSeek();
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
            if (!e->isAutoRepeat() && e->modifiers() == Qt::NoModifier) {
                m_spaceArmed = true;
                m_spaceConsumed = false;
                m_canvas->setSpacePan(true);
            }
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
        case Qt::Key_C:
            if (e->modifiers() & Qt::ControlModifier) {
                if (isVideo() && e->modifiers().testFlag(Qt::ShiftModifier)) copyVideoFrame();
                else copy();
            }
            else m_tools->setTool(ToolType::Crop);
            break;
        case Qt::Key_D: if (e->modifiers() & Qt::ControlModifier)
                            m_tools->duplicateSelection(QPointF(8,8));
                        else QWidget::keyPressEvent(e);
                        break;
        case Qt::Key_S:
            if ((e->modifiers() & Qt::ControlModifier) && (e->modifiers() & Qt::ShiftModifier)) saveProject();
            else if (e->modifiers() & Qt::ControlModifier) save();
            break;
        case Qt::Key_O:
            if (e->modifiers() & Qt::ControlModifier) { openProjectDialog(); break; }
            if (isVideo() && m_timeline) {
                m_timeline->setTrimRange(m_timeline->trimIn(), m_timeline->position());
                applyTrimRange(m_timeline->trimIn(), m_timeline->trimOut());
                break;
            }
            QWidget::keyPressEvent(e);
            break;
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
            m_spaceArmed = false;
            if (m_tools->cancelActive()) break;
            if (m_canvas->spacePanActive()) { m_canvas->setSpacePan(false); break; }
            close();
            break;
        default: QWidget::keyPressEvent(e);
    }
}

void EditorWindow::keyReleaseEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Space) {
        if (e->isAutoRepeat()) { e->accept(); return; }
        m_canvas->setSpacePan(false);
        const bool play = isVideo() && m_spaceArmed && !m_spaceConsumed;
        m_spaceArmed = false;
        if (play) togglePlayback();
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

}

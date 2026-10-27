#include "editor/editorwindow.h"
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
    // Exact once the current edit is exported; before that no guess (C7).
    const bool exported = isVideo() && hasVideoEdits() && m_cachedVideoRevision == m_videoRevision
        && !m_cachedVideoPath.isEmpty();
    m_exportPanel->setKnownSize(exported ? std::optional<qint64>(QFileInfo(m_cachedVideoPath).size()) : std::nullopt);
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

} // namespace eddy

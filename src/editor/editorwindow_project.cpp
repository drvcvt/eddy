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
    refreshMasks();
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
        m_recoveryIdle->start();
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
        if (!claimRecovery()) {
            m_recoveryPaused = true;
            return;
        }
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
            if (m_recoveryAsset.name.isEmpty()) dropRecovery();   // a first snapshot leaves nothing behind
        } else {
            m_recoveryAsset = asset;
            RecoveryStore().touch(id, source, QFileInfo(source.isEmpty() ? QStringLiteral("image.png") : source).fileName());
            emit recoveryWritten(RecoveryStore().manifestFor(id));
            if (m_recoveryPending && !m_closeAfterRecovery) m_recoveryIdle->start();
        }
        if (m_closeAfterRecovery) {
            m_closeAfterRecovery = false;
            close();
        }
    });
}

bool EditorWindow::claimRecovery() {
    const RecoveryStore store;
    m_recoveryId = store.create();
    if (m_recoveryId.isEmpty()) return false;
    m_recoveryLock = std::make_unique<QLockFile>(store.lockFileFor(m_recoveryId));
    m_recoveryLock->setStaleLockTime(0);
    m_recoveryLock->tryLock(0);
    return true;
}

void EditorWindow::dropRecovery() {
    m_recoveryLock.reset();
    RecoveryStore().discard(m_recoveryId);
    m_recoveryId.clear();
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
        if (!claimRecovery()) return;
        const QString assets = projectAssetsDir(store.manifestFor(m_recoveryId));
        asset = m_media.path.isEmpty() ? storeImageAsset(m_media.image, assets) : storeAsset(m_media.path, assets);
        if (!asset.ok) {
            dropRecovery();
            return;
        }
    }
    ProjectSnapshot project = projectSnapshot();
    project.asset = asset.name;
    project.sha256 = asset.sha256;
    project.assetSize = asset.size;
    if (writeProject(store.manifestFor(m_recoveryId), project).ok) {
        m_recoveryPending = false;
        store.touch(m_recoveryId, source, QFileInfo(source.isEmpty() ? QStringLiteral("image.png") : source).fileName());
    } else if (m_recoveryAsset.name.isEmpty()) {
        dropRecovery();
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

} // namespace eddy

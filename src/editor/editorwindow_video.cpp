#include "editor/editorwindow.h"
#include "app/fills.h"
#include "bars/stepbar.h"
#include "bars/selectionbar.h"
#include "canvas/snapping.h"
#include "items/labels/stepitem.h"
#include "timeline/lanes/audiowaveform.h"
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
#include "timeline/lanes/zoomlane.h"
#include "timeline/bars/zoombar.h"
#include "timeline/bars/fragmentbar.h"
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

// Timed items show only inside their window, like the export (studio plan 5).
void EditorWindow::applyTimeWindows(qint64 sourceMs) {
    if (!isVideo()) return;
    m_tools->setVideoTime(sourceMs, m_media.video.durationMs);
    for (QGraphicsItem *item : m_scene->items()) {
        auto *annotation = dynamic_cast<AnnotationItem *>(item);
        if (!annotation || (!dynamic_cast<RedactItem *>(item) && !dynamic_cast<SpotlightItem *>(item))) continue;
        const auto window = annotation->timeWindow();
        const bool shows = !window || (sourceMs >= window->first && sourceMs < window->second);
        if (item->isVisible() == shows) continue;
        if (!shows && item->isSelected()) item->setSelected(false);
        item->setVisible(shows);
    }
}

void EditorWindow::refreshMasks() {
    if (!m_timeline) return;
    QVector<VideoTimeline::MaskBlock> masks;
    for (QGraphicsItem *item : m_scene->items(Qt::AscendingOrder)) {
        auto *annotation = dynamic_cast<AnnotationItem *>(item);
        if (!annotation || !annotation->timeWindow()) continue;
        QString label = tr("Spotlight");
        if (auto *redact = dynamic_cast<RedactItem *>(item))
            label = RedactItem::isBlur(redact->mode()) ? tr("Blur") : tr("Black");
        masks.append({quintptr(item), annotation->timeWindow()->first, annotation->timeWindow()->second,
                      label, item->isSelected()});
    }
    m_timeline->setMasks(masks);
}

void EditorWindow::setTimeScope(AnnotationItem *item, bool fromPlayhead) {
    if (!item || !m_timeline) return;
    const AnnotationItem::TimeWindow after = fromPlayhead
        ? AnnotationItem::TimeWindow(std::pair{AnnotationItem::windowStart(m_timeline->position(), m_media.video.durationMs),
                                               m_media.video.durationMs})
        : AnnotationItem::TimeWindow();
    if (after == item->timeWindow()) return;
    m_undo->push(new SetTimeWindowCommand(item, item->timeWindow(), after, [this] {
        refreshMasks();
        applyTimeWindows(m_timeline->position());
        refreshRedactBar();
        refreshSpotlightBar();
        onVideoContentChanged();
    }));
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
        || m_exportSettings != ExportSettings{} || !m_studio.fragments.isEmpty()
        || (!m_studio.audio && m_media.video.hasAudio);
}

// Start or End at the playhead. Past the other bound, that bound moves to the clip's
// edge, so the range opens up instead of squeezing to a single frame.
void EditorWindow::setTrimStartAtPlayhead() {
    const qint64 at = m_timeline->position();
    applyTrimRange(at, at < m_timeline->trimOut() ? m_timeline->trimOut() : m_timeline->duration());
}

void EditorWindow::setTrimEndAtPlayhead() {
    const qint64 at = m_timeline->position();
    applyTrimRange(at > m_timeline->trimIn() ? m_timeline->trimIn() : 0, at);
}

void EditorWindow::applyTrimRange(qint64 inMs, qint64 outMs, int mergeKey) {
    if (!isVideo() || !m_timeline) return;
    m_timeline->setTrimRange(inMs, outMs);
    inMs = m_timeline->trimIn();
    outMs = m_timeline->trimOut();
    if (inMs == m_trimInMs && outMs == m_trimOutMs) return;
    // A range that holds only cut pieces would play and export nothing: keep the old one.
    if (TimeMap(m_media.video.durationMs, inMs, outMs, m_studio.fragments).pieces().isEmpty()) {
        m_timeline->setTrimRange(m_trimInMs, m_trimOutMs);
        m_toast->showMessage(tr("Keep at least part of the clip"));
        return;
    }
    m_undo->push(new SetTrimRangeCommand(
        m_trimInMs, m_trimOutMs, inMs, outMs,
        [this](qint64 nextIn, qint64 nextOut) { setTrimRangeState(nextIn, nextOut); }, mergeKey));
}

qint64 EditorWindow::editedMs(qint64 sourceMs) const {
    return qRound64(clipMap().toOutputAfter(double(sourceMs)));
}

qint64 EditorWindow::sourceFromEdited(qint64 ms) const {
    return qRound64(clipMap().toSource(double(ms)));   // a seam belongs to the later piece
}

QString EditorWindow::widestTime() const {
    return formatPreciseTime(qRound64(m_media.video.durationMs / fragments::kMinSpeed));
}

void EditorWindow::setTrimRangeState(qint64 inMs, qint64 outMs) {
    if (!m_timeline) return;
    m_timeline->setTrimRange(inMs, outMs);
    m_trimInMs = m_timeline->trimIn();
    m_trimOutMs = m_timeline->trimOut();
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

void EditorWindow::requestVideoSeek(qint64 position) {
    ensureVideoPlayer();
    if (holdingPause()) stopPauseHold(false);   // P: a seek ends a hold; the next pass holds again
    m_pauseDone = -1;
    m_seekTarget = qBound<qint64>(0, position, qMax<qint64>(0, m_media.video.durationMs - 1));
    m_seekSettling = true;
    m_seekSettleTimer->start();
    if (m_timeLabel) m_timeLabel->setText(formatPreciseTime(editedMs(m_seekTarget)));
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
    // Dragging: a seek sent before the last one showed a frame cancels it, and the
    // picture freezes. Wait for that frame (at most 150 ms); the release always seeks.
    constexpr qint64 kSeekPatienceMs = 150;
    if ((m_timelineActive || m_keyStepping) && m_seekInFlight && m_seekFlight.elapsed() < kSeekPatienceMs) {
        m_seekTimer->start();
        return;
    }
    m_seekInFlight = true;
    m_seekFlight.restart();
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
        if (m_resumeAfterSeek && m_seekTarget >= m_trimInMs && m_seekTarget < m_trimOutMs) {
            // Play pressed while the end-of-playback seek settled: from the top, not one frame.
            if (m_seekTarget >= lastFrameMs()) m_player->setPosition(m_trimInMs);
            m_player->play();
        }
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
    // The pill says it ("Prepare drag"); the status keeps a pending close in view.
    if (m_exportStatus && !closePending()) {
        m_exportStatus->setText(QString());
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
        // An edit made the running export stale: stop it rather than let it finish
        // first. finishVideoExportCache then starts the fresh one.
        if (m_videoExportRevision != m_videoRevision && m_videoExportCancel) m_videoExportCancel->store(true);
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
    m_videoExportRevision = revision;
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
    request.motionBlur = m_studio.motionBlur;
    request.includeAudio = m_studio.audio;
    request.maxBytes = qint64(m_exportSettings.limitMb) * 1'000'000;   // C7: the chat preset's 10 MB
    m_videoExportCancel = std::make_shared<std::atomic_bool>(false);
    request.cancelled = [cancel = m_videoExportCancel] { return cancel->load(); };
    QPointer<EditorWindow> receiver(this);
    request.progress = [receiver, revision](int percent) {
        QMetaObject::invokeMethod(qApp, [receiver, revision, percent] {
            if (receiver && receiver->m_videoExportInProgress && receiver->m_videoExportCancel
                && !receiver->m_videoExportCancel->load()
                && receiver->m_videoRevision == revision && receiver->m_dragPill) {
                receiver->m_dragPill->setProgress(percent);   // the pill fills (C8)
            }
        }, Qt::QueuedConnection);
    };
    for (QGraphicsItem *item : m_scene->items())
        if (auto *redact = dynamic_cast<RedactItem *>(item)) {
            if (const auto window = redact->timeWindow()) {
                for (const QRect &rect : redact->blurRectsInScene())
                    request.timedBlurs.append({rect, window->first, window->second});
            } else {
                request.blurRects += redact->blurRectsInScene();
            }
        }
    request.timedOverlays = renderTimedOverlays();
    m_videoExportInProgress = true;
    if (m_dragPill) {
        m_dragPill->setEnabled(false);
        m_dragPill->setBusy(true);
    }
    // The pill shows the export; the status keeps its words for what needs reading.
    if (m_exportStatus && !closePending()) {
        m_exportStatus->setText(QString());
        m_exportStatus->setToolTip(tr("Video export status"));
    }
    if (m_exportCancel) fadeVisible(m_exportCancel, true);

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
    if (m_exportCancel) fadeVisible(m_exportCancel, false);
    const bool current = result.ok && hasVideoEdits() && revision == m_videoRevision;
    if (current) {
        if (!m_cachedVideoPath.isEmpty() && m_cachedVideoPath != path
            && !m_clipboardVideoPaths.contains(m_cachedVideoPath)
            && !m_videoIpcPaths.contains(m_cachedVideoPath)) {
            QFile::remove(m_cachedVideoPath);
        }
        m_cachedVideoPath = path;
        m_cachedVideoRevision = revision;
        if (m_exportPanel) m_exportPanel->setKnownSize(QFileInfo(path).size());   // now exact
        if (m_dragPill) {
            m_dragPill->setProgress(100);
            m_dragPill->setPreparationNeeded(false);
            m_dragPill->setEnabled(true);
        }
        if (m_exportStatus && !closePending()) m_exportStatus->setText(QString());
    } else {
        QFile::remove(path);
        if (cancelled) {
            if (m_exportStatus && !closePending()) m_exportStatus->setText(QString());
        } else if (!result.ok && revision == m_videoRevision) {
            if (m_exportStatus) {
                m_exportStatus->setText(tr("Export failed"));
                m_exportStatus->setToolTip(result.error);
            }
            std::fprintf(stderr, "eddy: %s\n", qPrintable(result.error));
        }
    }

    // A cancelled run fails nothing: Cancel already answered the requests it had, and one
    // made since (Copy right after Cancel) is served by the export scheduled below.
    if (revision == m_videoRevision && m_videoStatusRequested && !cancelled) {
        if (result.ok)
            completePendingVideoActions(m_cachedVideoPath, true);
        else
            failPendingVideoActions(result.error);
    }

    if (m_dragPill) m_dragPill->setBusy(false);
    const bool needsFreshExport = hasVideoEdits() && revision != m_videoRevision;
    if (m_videoStatusRequested && (m_videoExportPending || needsFreshExport)) {
        m_videoExportPending = false;
        scheduleVideoExportCache(100);
    } else if (m_closeAfterVideoExport) {
        m_closeAfterVideoExport = false;
        // A failed export keeps the window: closing would drop what was asked for (mt: the
        // data would be corrupt or lost). The status names the failure.
        if (!result.ok && !cancelled && revision == m_videoRevision) {
            if (m_toast) m_toast->showMessage(tr("The export failed; eddy stays open"), 6000);
        } else {
            close();
        }
    }
}

void EditorWindow::cancelVideoExport() {
    if (!m_videoExportInProgress || !m_videoExportCancel) return;
    // ffmpeg is killed on the export thread; finishVideoExportCache cleans up.
    m_videoExportCancel->store(true);
    if (m_videoExportTimer) m_videoExportTimer->stop();
    m_videoExportPending = false;
    failPendingVideoActions();
    if (m_exportCancel) fadeVisible(m_exportCancel, false);
    if (m_dragPill) m_dragPill->setBusy(false);
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
                if (result.ok) receiver->close();   // a failed handoff keeps the window open
                else if (receiver->m_toast)
                    receiver->m_toast->showMessage(tr("The handoff failed; eddy stays open"), 6000);
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

} // namespace eddy

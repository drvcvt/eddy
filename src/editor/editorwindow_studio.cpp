#include "editor/editorwindow.h"
#include "app/fills.h"
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
    if (m_outputAudio) {
        const QSignalBlocker quiet(m_outputAudio);
        m_outputAudio->setChecked(doc.audio);
        fadeVisible(m_noAudio, !doc.audio && m_media.video.hasAudio);
    }
    if (m_exportPanel) m_exportPanel->setSound(doc.audio, isVideo() && m_media.video.hasAudio);
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
    camera.motionBlur = m_studio.motionBlur;
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
    // Presets (studio plan 6.10): applying one belongs to the popover session.
    const QVector<StudioPreset> presets = loadStudioPresets(configPath());
    QStringList presetNames;
    for (const StudioPreset &preset : presets) presetNames << preset.name;
    popover->setPresets(presetNames);
    auto applyPreset = [this](const StudioPreset &preset, StudioDocument &doc) {
        doc.style = preset.style;
        doc.motion = preset.motion;
        for (ZoomSegment &z : doc.zooms) z.motion = preset.motion;
    };
    connect(popover, &StudioPopover::presetChosen, this, [this, presets, applyPreset](int index) {
        StudioDocument doc = m_studio;
        applyPreset(presets.value(index), doc);
        setStudioDocument(doc);
    });
    connect(popover, &StudioPopover::presetSaveRequested, this, [this, popover] {
        popover->close();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        const DeliverResult saved = saveStudioPreset(configPath(), {name, m_studio.style, m_studio.motion});
        m_toast->showMessage(saved.ok ? tr("Preset saved") : tr("Preset not saved"));
    });
    connect(popover, &StudioPopover::presetImportRequested, this, [this, popover, applyPreset] {
        popover->close();
        const QString path = QFileDialog::getOpenFileName(this, tr("Import preset"), {}, tr("Studio preset (*.json)"));
        if (path.isEmpty()) return;
        QFile file(path);
        QString error;
        const auto preset = file.open(QIODevice::ReadOnly) ? importStudioPreset(file.readAll(), &error) : std::nullopt;
        if (!preset) {
            std::fprintf(stderr, "eddy: %s\n", qPrintable(error));
            m_toast->showMessage(tr("Not a usable Studio preset"));
            return;
        }
        saveStudioPreset(configPath(), *preset);
        editStudio([&](StudioDocument &d) { applyPreset(*preset, d); });
        m_toast->showMessage(tr("Preset imported"));
    });
    connect(popover, &StudioPopover::presetExportRequested, this, [this, popover] {
        popover->close();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Export preset"), tr("Name"), QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export preset"), name + QStringLiteral(".json"),
                                                          tr("Studio preset (*.json)"));
        if (path.isEmpty()) return;
        QString error;
        const QByteArray bytes = exportStudioPreset({name, m_studio.style, m_studio.motion}, &error);
        QSaveFile file(path);
        if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            std::fprintf(stderr, "eddy: %s\n", qPrintable(error.isEmpty() ? file.errorString() : error));
            m_toast->showMessage(tr("Preset not exported"));
            return;
        }
        m_toast->showMessage(tr("Preset exported"));
    });
    connect(popover, &StudioPopover::motionBlurChanged, this, [this](int strength) {
        StudioDocument doc = m_studio;
        doc.motionBlur = strength;
        setStudioDocument(doc);
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
    popover->move(theme::popoverPosition(button, m_toolbar, popover->size(), true));
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
    // A recording with its pointer is made for zooms: the lane shows from the start (C5).
    m_timeline->setZoomLaneVisible(m_studio.style.active() || !m_studio.zooms.isEmpty() || m_cursorTrack);
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
    return m_cameraPath.rectAt(cameraTime());
}

// Output time of what the canvas shows: the presented frame while playing.
double EditorWindow::cameraTime() const {
    const bool playing = m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
    const qint64 source = playing && m_presentedStart >= 0 ? m_presentedStart : m_timeline->position();
    return source <= m_trimInMs ? 0.0 : m_timeMap.toOutputAfter(double(source));
}

void EditorWindow::updateCamera() {
    if (!isVideo() || !m_timeline) return;
    const bool cropping = m_crop && m_crop->active();
    m_canvas->setCamera(cropping ? QRectF() : currentCamera());
    if (m_miniMap && m_miniMap->isVisible()) m_miniMap->setCamera(currentCamera());
}

void EditorWindow::selectZoom(quint32 id) {
    if (zoomlane::indexOf(m_studio.zooms, id) < 0) id = 0;
    if (id && m_timeline) m_timeline->clearMark();   // one target at a time
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
    // Where the pointer was (C5): at the asked time, else its first visible sample in the
    // zoom's span; without a recorded pointer the last point used, else the middle.
    std::optional<QPointF> pointer;
    if (m_cursorTrack) {
        pointer = m_cursorTrack->positionAt(sourceMs);
        for (const CursorSample &s : m_cursorTrack->samples) {
            if (pointer) break;
            if (s.visible && s.ms >= span->first && s.ms < span->second) pointer = s.pos;
        }
    }
    z.point = pointer ? *pointer : m_lastZoomPoint.isNull() ? QRectF(cameraBase()).center() : m_lastZoomPoint;
    z.motion = m_studio.motion;
    if (m_player) m_player->pause();
    editStudio([&](StudioDocument &d) { zoomlane::insert(d.zooms, z); });
    selectZoom(z.id);
}

void EditorWindow::selectFragment(int index) {
    if (m_studio.fragments.isEmpty() || index >= m_studio.fragments.size()) index = -1;
    if (index >= 0 && m_timeline) m_timeline->clearMark();
    if (index >= 0 && m_selectedZoom) {
        m_selectedZoom = 0;
        if (m_timeline) m_timeline->setSelectedZoom(0);
        updateCamera();
    }
    m_selectedFragment = index;
    if (m_timeline) m_timeline->setSelectedFragment(index);
    refreshZoomUi();
}

// Cuts a kept fragment (it stays selected, its bar offers Restore) or restores a cut one.
// A restore may melt the fragment into its neighbours, so nothing stays selected after it.
void EditorWindow::toggleFragmentCut(int index) {
    if (index < 0 || index >= m_studio.fragments.size()) return;
    QVector<Fragment> list = m_studio.fragments;
    const bool restoring = list[index].removed;
    if (!fragments::setRemoved(list, index, !restoring)
        || TimeMap(m_media.video.durationMs, m_trimInMs, m_trimOutMs, list).pieces().isEmpty()) {
        m_toast->showMessage(tr("Keep at least part of the clip"));
        return;
    }
    editStudio([&](StudioDocument &d) { d.fragments = list; });
    if (restoring) selectFragment(-1);
}

void EditorWindow::restoreCut(int index) {
    if (index >= 0 && index < m_studio.fragments.size() && m_studio.fragments[index].removed)
        toggleFragmentCut(index);
}

// One undo step: the marked range becomes one cut, melted with cuts beside it. The
// playhead lands on the first kept frame after it (or the last before, at the end).
void EditorWindow::cutMarkedRange() {
    if (!m_timeline || !m_timeline->hasMark()) return;
    auto [from, to] = m_timeline->markRange();
    // Ends within kMinFragmentMs of the trim go onto it: no sliver of kept output there.
    if (from - m_trimInMs < fragments::kMinFragmentMs) from = m_trimInMs;
    if (m_trimOutMs - to < fragments::kMinFragmentMs) to = m_trimOutMs;
    QVector<Fragment> list = m_studio.fragments;
    if (!fragments::cutRange(list, from, to, m_media.video.durationMs)
        || TimeMap(m_media.video.durationMs, m_trimInMs, m_trimOutMs, list).pieces().isEmpty()) {
        m_toast->showMessage(tr("Keep at least part of the clip"));
        return;
    }
    ensureVideoPlayer();
    if (m_player) m_player->pause();
    m_resumeAfterSeek = false;
    m_timeline->clearMark();
    editStudio([&](StudioDocument &d) { d.fragments = list; });
    selectFragment(-1);
    const TimeMap map(m_media.video.durationMs, m_trimInMs, m_trimOutMs, m_studio.fragments);
    qint64 at = qRound64(map.toSource(map.toOutputAfter(to)));
    if (at >= m_trimOutMs) at = qMax(m_trimInMs, lastFrameMs());
    m_timeline->setPosition(at);
    m_timeline->ensureVisible(at);
    requestVideoSeek(at);
    flushVideoSeek();
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
        // A following base is taken from where it is now, not where it once was placed.
        const bool following = m_studio.keepFollowsCursor && m_cursorTrack;
        const QPointF c = (following ? m_cameraPath.homeAt(cameraTime()) : m_studio.keepCenter) + delta;
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
    fadeVisible(m_fragmentBar, fragment);
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
    QList<QGraphicsItem *> hiddenForOverlay;
    for (QGraphicsItem *item : m_scene->items()) {
        auto *redact = dynamic_cast<RedactItem *>(item);
        auto *annotation = dynamic_cast<AnnotationItem *>(item);
        // Blur is ffmpeg's; timed items get overlays of their own.
        const bool timed = annotation && annotation->timeWindow();
        if ((timed || (redact && RedactItem::isBlur(redact->mode()))) && item->isVisible()) {
            hiddenForOverlay.append(item);
            item->hide();
        }
    }
    const bool hadBackground = m_backgroundItem != nullptr;
    const bool wasVisible = hadBackground && m_backgroundItem->isVisible();
    m_renderingVideoOverlay = true;
    const int renderGeneration = ++m_videoOverlayRenderGeneration;
    if (hadBackground) m_backgroundItem->setVisible(false);
    QImage overlay = renderToImage(*m_scene, m_media.nativeSize());
    if (hadBackground) m_backgroundItem->setVisible(wasVisible);
    for (QGraphicsItem *item : hiddenForOverlay) item->show();
    for (QGraphicsItem *item : selection) item->setSelected(true);
    QTimer::singleShot(50, this, [this, renderGeneration]{
        if (renderGeneration == m_videoOverlayRenderGeneration)
            m_renderingVideoOverlay = false;
    });
    return overlay;
}

// One overlay per timed spotlight or blackened redaction, each shown alone
// (studio plan 6.7); timed blur goes to ffmpeg as timed blur regions.
QVector<TimedOverlay> EditorWindow::renderTimedOverlays() {
    QVector<AnnotationItem *> timed;
    for (QGraphicsItem *item : m_scene->items()) {
        auto *annotation = dynamic_cast<AnnotationItem *>(item);
        auto *redact = dynamic_cast<RedactItem *>(item);
        if (annotation && annotation->timeWindow() && !(redact && RedactItem::isBlur(redact->mode())))
            timed.append(annotation);
    }
    QVector<TimedOverlay> out;
    if (timed.isEmpty()) return out;
    const auto selection = m_scene->selectedItems();
    m_scene->clearSelection();
    QHash<QGraphicsItem *, bool> visible;
    for (QGraphicsItem *item : m_scene->items()) visible.insert(item, item->isVisible());
    for (AnnotationItem *item : timed) {
        for (QGraphicsItem *other : m_scene->items()) other->setVisible(other == item);
        out.append({renderToImage(*m_scene, m_media.nativeSize()), item->timeWindow()->first, item->timeWindow()->second});
    }
    for (auto it = visible.cbegin(); it != visible.cend(); ++it) it.key()->setVisible(it.value());
    for (QGraphicsItem *item : selection) item->setSelected(true);
    return out;
}

} // namespace eddy

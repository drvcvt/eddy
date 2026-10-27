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
    if (isVideo() && m_timeline && (m_timeline->hasMark() || m_timeline->markOpen())
        && m_scene->selectedItems().isEmpty() && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
        cutMarkedRange();   // an open mark of no width yet cuts nothing; the key is still taken
        e->accept();
        return;
    }
    if (m_selectedFragment >= 0 && !m_selectedZoom && m_scene->selectedItems().isEmpty()
        && (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)) {
        toggleFragmentCut(m_selectedFragment);   // cut, and on a cut piece restore
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
        if (m_timeline && m_timeline->marking()) { m_timeline->cancelInteraction(); e->accept(); return; }
        if (m_timeline && (m_timeline->hasMark() || m_timeline->markOpen())) {
            m_timeline->clearMark(); e->accept(); return;
        }
        if (m_selectedZoom) { selectZoom(0); e->accept(); return; }
        if (m_selectedFragment >= 0) { selectFragment(-1); e->accept(); return; }
    }
    switch (e->key()) {
        case Qt::Key_I:
            if (isVideo() && m_timeline && e->modifiers() == Qt::ShiftModifier) { m_timeline->markFromPlayhead(); break; }
            if (isVideo() && m_timeline) { setTrimStartAtPlayhead(); break; }
            QWidget::keyPressEvent(e);
            break;
        case Qt::Key_J:
        case Qt::Key_L:
            if (isVideo() && m_timeline) {   // the same frame steps as the arrows, cuts skipped
                stepPlayhead(stepFrames(clipMap(), m_media.video.durationMs, m_media.video.fps,
                                        m_timeline->position(), e->key() == Qt::Key_J ? -1 : 1), e->isAutoRepeat());
                break;
            }
            QWidget::keyPressEvent(e);
            break;
        case Qt::Key_Home:
        case Qt::Key_End:
            if (isVideo() && m_timeline && e->modifiers() == Qt::NoModifier) {
                // Start, or the last frame of the range, where playback parks.
                stepPlayhead(e->key() == Qt::Key_Home ? m_trimInMs : qMax(m_trimInMs, lastFrameMs()), false);
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
            if (m_tools->nudgeSelection(delta)) break;
            // Nothing to nudge: on a video the arrows move the playhead. Left and Right a
            // frame (Shift: a second as it plays), Up and Down to the previous or next edge.
            if (isVideo() && m_timeline && (e->modifiers() & ~Qt::ShiftModifier & ~Qt::KeypadModifier) == 0) {
                const qint64 at = m_timeline->position();
                const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
                const int sign = e->key() == Qt::Key_Left || e->key() == Qt::Key_Up ? -1 : 1;
                if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
                    stepPlayhead(shift ? stepOutput(clipMap(), m_media.video.durationMs, m_media.video.fps, at, sign * 1000.0)
                                       : stepFrames(clipMap(), m_media.video.durationMs, m_media.video.fps, at, sign),
                                 e->isAutoRepeat());
                } else if (const auto edge = nextEdge(clipMap(), playheadEdges(), at, sign)) {
                    stepPlayhead(*edge, e->isAutoRepeat());
                }
                break;
            }
            QWidget::keyPressEvent(e);
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
        case Qt::Key_N: m_tools->setTool(ToolType::Step); break;
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
            if (isVideo() && m_timeline && e->modifiers() == Qt::ShiftModifier) { m_timeline->markToPlayhead(); break; }
            if (isVideo() && m_timeline) { setTrimEndAtPlayhead(); break; }
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
            if (closePending()) { withdrawClose(); break; }   // the second Esc takes the close back
            close();
            break;
        default: QWidget::keyPressEvent(e);
    }
}

TimeMap EditorWindow::clipMap() const {
    return TimeMap(m_media.video.durationMs, 0, m_media.video.durationMs, m_studio.fragments);
}

// Where Up and Down stop: the clip's ends, Start and End, every seam between pieces, and
// the ends of every zoom and timed mask.
QVector<qint64> EditorWindow::playheadEdges() const {
    const double fps = m_media.video.fps > 0 ? m_media.video.fps : 30.0;
    const qint64 lastFrame = qRound64((std::ceil(m_media.video.durationMs * fps / 1000.0) - 1) * 1000.0 / fps);
    QVector<qint64> edges{0, qMax<qint64>(0, lastFrame), m_trimInMs, qMax(m_trimInMs, lastFrameMs())};
    for (const Fragment &f : m_studio.fragments) if (f.startMs > 0) edges.append(f.startMs);
    for (const ZoomSegment &z : m_studio.zooms) edges << z.startMs << z.endMs;
    for (QGraphicsItem *item : m_scene->items())
        if (auto *annotation = dynamic_cast<AnnotationItem *>(item); annotation && annotation->timeWindow())
            edges << annotation->timeWindow()->first << annotation->timeWindow()->second;
    return edges;
}

// Pauses, moves the playhead and keeps it in view. Held down, the seeks queue behind the
// frame in flight; the key's release seeks the last one.
void EditorWindow::stepPlayhead(qint64 target, bool held) {
    ensureVideoPlayer();
    if (m_player) m_player->pause();
    m_resumeAfterSeek = false;
    m_keyStepping = held;
    m_timeline->setPosition(target);
    m_timeline->ensureVisible(target);
    requestVideoSeek(target);
    if (!held) flushVideoSeek();
}

void EditorWindow::keyReleaseEvent(QKeyEvent *e) {
    if (m_keyStepping && !e->isAutoRepeat()) {
        m_keyStepping = false;
        flushVideoSeek();
    }
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

} // namespace eddy

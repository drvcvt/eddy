// The timeline under the pointer and keys: what a press starts, a drag moves, a release
// lands, Esc takes back; the wheel, the tooltip and the right-click menu.
#include "timeline/videotimeline.h"
#include "timeline/lanes/audiowaveform.h"
#include "app/theme.h"
#include "app/spring.h"
#include "timeline/lanes/zoomlane.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMenu>
#include <cmath>

namespace eddy {

// The right-click menu: a ghost back, the pause here, the waveform, a zoom here, the view.
void VideoTimeline::contextMenu(QPoint pos) {
    emit hoverLeft();
    Drag part = Drag::None;
    if (const quint32 id = zoomAt(pos, &part)) {
        setSelectedZoom(id);
        emit zoomSelected(id);
        emit zoomMenuRequested(id, mapToGlobal(pos));
        return;
    }
    QMenu menu(this);
    theme::roundMenu(&menu);
    if (const auto ghost = ghostAt(pos)) {   // G: the gap closed, or what was cut back
        menu.addAction(tr("Close gap\tDouble-click"), this, [this, range = *ghost] { setGapClosed(range, true); });
        menu.addAction(tr("Restore"), this, [this, range = *ghost] { restoreGhost(range); });
        menu.addSeparator();
    } else if (const auto seam = closedAt(pos)) {
        menu.addAction(tr("Show cut\tDouble-click"), this, [this, range = *seam] { setGapClosed(range, false); });
        menu.addAction(tr("Restore"), this, [this, range = *seam] { restoreGhost(range); });
        menu.addSeparator();
    }
    if (const int piece = pauseAt(pos); piece >= 0) {   // P: what the pause shows, or close it
        const bool black = m_axis.pieces()[piece].pauseBlack;
        QAction *hold = menu.addAction(tr("Hold last frame"), this, [this, piece] { editPause(piece, std::nullopt, false); });
        QAction *dark = menu.addAction(tr("Black"), this, [this, piece] { editPause(piece, std::nullopt, true); });
        hold->setCheckable(true);
        hold->setChecked(!black);
        dark->setCheckable(true);
        dark->setChecked(black);
        menu.addAction(tr("Close pause\tDouble-click"), this, [this, piece] { editPause(piece, 0, std::nullopt); });
        menu.addSeparator();
    }
    if (m_waveform && !m_waveform->silent()) {   // a silent track has nothing to show
        QAction *show = menu.addAction(tr("Show waveform"));
        show->setCheckable(true);
        show->setChecked(m_waveformVisible);
        connect(show, &QAction::toggled, this, &VideoTimeline::setWaveformVisible);
        menu.addSeparator();
    }
    if (inZoomLane(pos)) {
        const qint64 time = timeForX(pos.x());
        menu.addAction(tr("Add zoom here"), this, [this, time] { emit zoomAddRequested(time); });
        menu.addSeparator();
    }
    // C2: the clip's dead air, found from its sound, pointer and picture.
    menu.addAction(tr("Cut pauses"), this, [this] { emit pausesRequested(true); });
    menu.addAction(tr("Speed up pauses (4×)"), this, [this] { emit pausesRequested(false); });
    menu.addSeparator();
    menu.addAction(tr("Zoom in\t+"), this, [this] { glideView([this] { zoomAt(2, m_position); }); });
    menu.addAction(tr("Zoom out\t−"), this, [this] { glideView([this] { zoomAt(0.5, m_position); }); });
    menu.addAction(tr("Fit clip\t0"), this, [this] { glideView([this] { fitClip(); }); });
    menu.exec(mapToGlobal(pos));
}

void VideoTimeline::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_duration <= 0) return;
    setFocus(Qt::MouseFocusReason);
    {
        Drag part = Drag::None;
        const int mask = maskAt(event->position(), &part);
        if (mask >= 0) {
            emit maskSelected(m_masks[mask].key);
            m_drag = part;
            m_dragMask = mask;
            m_maskBefore = m_masks[mask];
            m_zoomMoved = false;
            m_dragX = m_pointerX = event->position().x();
            m_grabOffset = timeForX(m_dragX) - (part == Drag::MaskEnd ? m_masks[mask].toMs : m_masks[mask].fromMs);
            m_edgePan.start();
            event->accept();
            return;
        }
        if (!maskLaneRect().isEmpty() && event->position().y() >= maskLaneRect().top() - 2) {
            event->accept();   // the empty mask lane does nothing
            return;
        }
    }
    if (inZoomLane(event->position())) {
        Drag part = Drag::None;
        const quint32 id = zoomAt(event->position(), &part);
        if (!id) {
            // A zoom comes on release without movement: a press that drags adds nothing (C5).
            m_drag = Drag::ZoomAdd;
            m_pressX = event->position().x();
            m_pressMoved = false;
            m_zoomAddAt = timeForX(m_pressX);
            event->accept();
            return;
        }
        setSelectedZoom(id);
        emit zoomSelected(id);
        const ZoomSegment &z = m_zooms[zoomlane::indexOf(m_zooms, id)];
        m_drag = part;
        m_dragZoom = id;
        m_zoomsBefore = m_zooms;
        m_zoomMoved = false;
        m_dragX = m_pointerX = event->position().x();   // the edge-pan timer reads it
        m_modifiers = event->modifiers();
        m_grabOffset = timeForX(m_dragX) - (part == Drag::ZoomEnd ? z.endMs : z.startMs);
        m_edgePan.start();
        event->accept();
        return;
    }
    const qreal x = event->position().x();
    int piece = -1;
    bool end = false;
    const bool onEdge = edgeAt(event->position(), &piece, &end);
    // P, G: Shift on a block or at a seam moves the piece right of it, and all after it:
    // pulling apart opens a pause. A start grip is its piece's; an end grip the next one's
    // where the two touch, else its own. Alt marks a range, and on a grip pulls finely.
    if (event->modifiers().testFlag(Qt::ShiftModifier)) {
        int moved = onEdge ? piece : pieceAt(event->position());
        if (onEdge && end && piece + 1 < m_axis.pieces().size()
            && m_axis.pieces()[piece + 1].srcStart == m_axis.pieces()[piece].srcEnd)
            moved = piece + 1;
        if (moved >= 0) {
            beginMove(moved, x);
            setCursor(Qt::ClosedHandCursor);
            emit interactionStarted(true);
            m_edgePan.start();
            event->accept();
            return;
        }
    }
    if (onEdge) {
        beginEdgeDrag(piece, end, x, event->modifiers());
        emit interactionStarted(true);
        m_edgePan.start();
        mouseMoveEvent(event);
        event->accept();
        return;
    }
    m_drag = event->modifiers().testFlag(Qt::AltModifier) ? Drag::Mark : Drag::Seek;
    m_beforePosition = m_position;
    m_dragX = m_pressX = x;
    m_pressMoved = false;
    if (m_drag == Drag::Mark) {
        m_markBefore = {m_markAnchor, m_markHead, m_markOpen};
        // An Alt-click marks from the mark's anchor, or from the playhead without one.
        m_markClickAnchor = m_markAnchor >= 0 ? m_markAnchor : m_position;
        m_markAnchor = m_markHead = snapMark(timeForX(x));
        m_markOpen = false;
    } else if (m_drag == Drag::Seek) {
        clearMark();
    }
    emit interactionStarted(false);
    m_edgePan.start();
    mouseMoveEvent(event);
    event->accept();
}

// What the area under the pointer does, for the tooltip: the gestures it takes.
QString VideoTimeline::hintAt(QPointF pos) const {
    if (pos.y() < kRulerH)
        return tr("Zoom\tWheel\nPan\tShift+wheel\nFit clip\t0\nMark a range to cut\tAlt+drag below");
    Drag part = Drag::None;
    if (inZoomLane(pos))
        return zoomAt(pos, &part) ? tr("Drag to move, pull an end to resize\nRemove\tDelete")
                                  : tr("Add a zoom here\tClick or Z");
    if (maskAt(pos, &part) >= 0) return tr("Drag to move, pull an end to resize");
    int piece = -1;
    bool end = false;
    if (const auto seam = closedAt(pos))
        return tr("Pull to trim this piece\nFine\tAlt\nShow the cut %1\u00a0s\tDouble-click")
            .arg((seam->second - seam->first) / 1000.0, 0, 'f', 1);
    if (edgeAt(pos, &piece, &end)) return tr("Pull to trim this piece\nFine\tAlt\nPull apart for a pause\tShift");
    if (const auto ghost = ghostAt(pos))
        return tr("Cut %1\u00a0s\nClose the gap\tDouble-click\nRestore\tRight-click").arg((ghost->second - ghost->first) / 1000.0, 0, 'f', 1);
    if (const int paused = pauseAt(pos); paused >= 0)
        return tr("Pause %1\u00a0s\nClose\tDouble-click\nHold or black\tRight-click")
            .arg(m_axis.pieces()[paused].pauseBefore / 1000.0, 0, 'f', 1);
    return {};
}

void VideoTimeline::mouseMoveEvent(QMouseEvent *event) {
    if (m_drag == Drag::None) setToolTip(hintAt(event->position()));
    if (m_drag == Drag::None) {
        const auto ghost = ghostAt(event->position());
        if (const qint64 lit = ghost ? ghost->first : -1; lit != m_hoverGhost) {   // G: a ghost lifts a step
            m_hoverGhost = lit;
            update();
        }
        Drag maskPart = Drag::None;
        if (maskAt(event->position(), &maskPart) >= 0
            || (!maskLaneRect().isEmpty() && event->position().y() >= maskLaneRect().top() - 2)) {
            setCursor(maskPart == Drag::MaskStart || maskPart == Drag::MaskEnd ? Qt::SizeHorCursor
                                                                             : Qt::PointingHandCursor);
            emit hoverLeft();
            return;
        }
        if (inZoomLane(event->position())) {
            Drag part = Drag::None;
            zoomAt(event->position(), &part);
            setCursor(part == Drag::ZoomStart || part == Drag::ZoomEnd ? Qt::SizeHorCursor
                                                                     : Qt::PointingHandCursor);
            if (m_hoverEdge >= 0) { m_hoverEdge = -1; update(); }
            emit hoverLeft();
            return;
        }
        const qreal x = event->position().x();
        int piece = -1;
        bool end = false;
        const int hover = edgeAt(event->position(), &piece, &end) ? piece * 2 + int(end) : -1;
        if (hover != m_hoverEdge) {
            m_hoverEdge = hover;
            update();
        }
        setCursor(hover >= 0 ? Qt::SizeHorCursor
                  : event->modifiers().testFlag(Qt::AltModifier) ? Qt::IBeamCursor
                  : event->modifiers().testFlag(Qt::ShiftModifier) && pieceAt(event->position()) >= 0 ? Qt::OpenHandCursor
                  : Qt::PointingHandCursor);
        emit hoverRequested(timeForX(x), event->pos());
        return;
    }
    moveDrag(event->position().x(), event->modifiers());
    event->accept();
}

void VideoTimeline::moveDrag(qreal x, Qt::KeyboardModifiers modifiers) {
    m_pointerX = x;
    m_modifiers = modifiers;
    if (zoomDragging()) { moveZoomDrag(x); return; }
    if (maskDragging()) { moveMaskDrag(x); return; }
    if (m_drag == Drag::Edge) { moveEdgeDrag(x); return; }
    if (m_drag == Drag::Move) { moveMove(x); return; }
    if (qAbs(x - m_pressX) >= 3) m_pressMoved = true;
    if (m_drag == Drag::ZoomAdd) return;
    if (m_drag == Drag::Mark) {
        if (!m_pressMoved) return;   // a Shift-click is decided on release
        m_markHead = snapMark(timeForX(x));
        m_position = m_markHead;   // the playhead rides the moving edge: the frame where the cut ends
        showMark();
        emit seekRequested(m_position);
        emit markChanged();
        update();
        return;
    }
    m_position = timeForX(x);
    // P: inside a pause the playhead stands where it was put; the canvas shows the frame held.
    const QRectF track = trackRect();
    const double at = m_viewStart + qBound<qreal>(0, (x - track.left()) / track.width(), 1) * (m_viewEnd - m_viewStart);
    const auto pause = m_display.pauseAt(at);
    const int piece = pause ? int(m_keptAt.indexOf(pause->piece)) : -1;
    m_pauseHead = piece >= 0 ? m_axis.pieces()[piece].pauseStart() + pause->intoMs : -1;
    emit seekRequested(m_position);
    update();
}

void VideoTimeline::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_drag == Drag::None) return;
    if (maskDragging()) {
        moveMaskDrag(event->position().x());
        m_drag = Drag::None;
        m_edgePan.stop();
        const MaskBlock &mask = m_masks[m_dragMask];
        if (mask.fromMs != m_maskBefore.fromMs || mask.toMs != m_maskBefore.toMs)
            emit maskWindowEdited(mask.key, mask.fromMs, mask.toMs, m_maskBefore.fromMs, m_maskBefore.toMs);
        update();
        event->accept();
        return;
    }
    if (zoomDragging()) {
        moveZoomDrag(event->position().x());
        m_drag = Drag::None;
        m_edgePan.stop();
        if (m_zooms != m_zoomsBefore) emit zoomsEdited(m_zoomsBefore, m_zooms);
        update();
        event->accept();
        return;
    }
    if (m_drag == Drag::ZoomAdd) {
        moveDrag(event->position().x(), event->modifiers());
        m_drag = Drag::None;
        if (!m_pressMoved && inZoomLane(event->position())) emit zoomAddRequested(m_zoomAddAt);
        event->accept();
        return;
    }
    if (m_drag == Drag::Mark) {
        moveDrag(event->position().x(), event->modifiers());
        if (!m_pressMoved) {   // Shift-click: from the old anchor or the playhead to here
            m_markAnchor = m_markClickAnchor;
            m_markHead = snapMark(timeForX(event->position().x()));
            m_position = m_markHead;
            emit seekRequested(m_position);
        }
        if (hasMark()) showMark();
        else { m_markAnchor = m_markHead = -1; m_markFade->jumpTo(0); }   // under a frame: nothing
        m_drag = Drag::None;
        m_edgePan.stop();
        emit markChanged();
        emit interactionFinished(false);
        emit hoverLeft();
        update();
        event->accept();
        return;
    }
    moveDrag(event->position().x(), event->modifiers());
    const bool clicked = m_drag == Drag::Seek && !m_pressMoved;
    if (m_drag == Drag::Move && m_moveNow > 0 && m_moveNow < m_minimumRange) {   // under a frame is none
        m_moveNow = 0;
        m_clipPreview = fragments::setPause(m_clipBefore, m_movePiece, 0, m_duration);
    }
    if (m_drag == Drag::Move && m_clipPreview != m_clipBefore) {
        const fragments::Clip before = m_clipBefore, after = m_clipPreview;
        setClip(after.inMs, after.outMs, after.list);   // glides on from where the drag left it
        m_drag = Drag::None;
        emit clipEdited(before, after, m_movePiece);
    }
    if (m_drag == Drag::Edge && m_clipPreview != m_clipBefore) {
        // The edit lands while the pulled edge still shows: the blocks glide on from there.
        const fragments::Clip before = m_clipBefore, after = m_clipPreview;
        setClip(after.inMs, after.outMs, after.list);
        m_drag = Drag::None;
        emit clipEdited(before, after, m_edgePiece);
    }
    m_drag = Drag::None;
    m_edgePan.stop();
    if (clicked) emit stripClicked(m_position);
    emit interactionFinished(false);
    emit hoverLeft();
    update();
    event->accept();
}

// A double click on empty space snaps what follows onto what is before: a ghost's gap closes
// (the cut stays cut), a pause closes (P). On a closed gap's seam it opens again.
void VideoTimeline::mouseDoubleClickEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (event->button() == Qt::LeftButton) {
        if (const auto ghost = ghostAt(pos)) { setGapClosed(*ghost, true); event->accept(); return; }
        if (const auto seam = closedAt(pos)) { setGapClosed(*seam, false); event->accept(); return; }
        if (const int piece = pauseAt(pos); piece >= 0) { editPause(piece, 0, std::nullopt); event->accept(); return; }
    }
    mousePressEvent(event);   // elsewhere a double click is two clicks
}

void VideoTimeline::leaveEvent(QEvent *event) {
    m_hoverEdge = -1;
    m_hoverGhost = -1;
    if (!interacting()) emit hoverLeft();
    setCursor(Qt::PointingHandCursor);
    update();
    QWidget::leaveEvent(event);
}

void VideoTimeline::cancelInteraction() {
    if (!interacting()) return;
    if (m_drag == Drag::ZoomAdd) { m_drag = Drag::None; return; }   // nothing began yet
    if (maskDragging()) {
        m_drag = Drag::None;
        m_edgePan.stop();
        m_masks[m_dragMask] = m_maskBefore;
        emit maskWindowPreviewed(m_maskBefore.key, m_maskBefore.fromMs, m_maskBefore.toMs);
        update();
        return;
    }
    if (zoomDragging()) {
        m_drag = Drag::None;
        m_edgePan.stop();
        m_zooms = m_zoomsBefore;
        emit zoomsPreviewed(m_zooms);
        update();
        return;
    }
    if (marking()) {   // Esc mid-drag: the mark and playhead from before the press
        m_markAnchor = m_markBefore.anchor;
        m_markHead = m_markBefore.head;
        m_markOpen = m_markBefore.open;
        if (hasMark()) showMark(); else m_markFade->jumpTo(0);
        emit markChanged();
    }
    m_drag = Drag::None;   // a pulled edge goes back: nothing was applied yet
    m_edgePan.stop();
    m_position = m_beforePosition;
    emit seekRequested(m_position);
    emit interactionFinished(true);
    emit hoverLeft();
    update();
}

void VideoTimeline::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        if (!interacting()) { QWidget::keyPressEvent(event); return; }
        cancelInteraction(); break;
    case Qt::Key_Plus: case Qt::Key_Equal: glideView([this] { zoomAt(2, m_position); }); break;
    case Qt::Key_Minus: glideView([this] { zoomAt(0.5, m_position); }); break;
    case Qt::Key_0: glideView([this] { fitClip(); }); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void VideoTimeline::wheelEvent(QWheelEvent *event) {
    // The wheel zooms at the pointer (C3; at once, like the canvas); Shift or a mostly
    // sideways touchpad swipe pans.
    const QPoint delta = event->pixelDelta().isNull() ? event->angleDelta() : event->pixelDelta() * 2;
    const bool pan = event->modifiers().testFlag(Qt::ShiftModifier) || qAbs(delta.x()) > qAbs(delta.y());
    if (pan)
        panBy(qRound64(-(delta.x() ? delta.x() : delta.y()) * (m_viewEnd - m_viewStart) / 1200.0));
    else if (delta.y())
        zoomAt(std::pow(1.2, delta.y() / 120.0), timeForX(event->position().x()));
    event->accept();
}

void VideoTimeline::focusOutEvent(QFocusEvent *event) {
    update();
    cancelInteraction();
    emit hoverLeft();
    QWidget::focusOutEvent(event);
}

}

// The clip model (K, docs/plans/2026-10/2026-10-27-clips.md): every kept piece is a block on
// the output's axis, every block edge a grip. Pulling one moves only that edge under the
// pointer; on release the edit lands and the blocks glide to where it puts them.
#include "timeline/videotimeline.h"
#include "app/spring.h"
#include "timeline/lanes/zoomlane.h"
#include <algorithm>

namespace eddy {

qreal VideoTimeline::xForOutput(double outMs) const {
    const QRectF track = trackRect();
    if (m_viewEnd <= m_viewStart) return track.left();
    return track.left() + track.width() * (outMs - m_viewStart) / (m_viewEnd - m_viewStart);
}

QVector<QRectF> VideoTimeline::pieceRects() const {
    const QRectF track = trackRect();
    const QVector<TimePiece> &pieces = m_axis.pieces();
    const qreal glide = m_ripple.size() == pieces.size() ? m_rippleGlide->value() : 0;
    QVector<QRectF> rects;
    for (int i = 0; i < pieces.size(); ++i) {
        const TimePiece &p = pieces[i];
        double outStart = p.outStart, outEnd = p.outEnd();
        if (m_drag == Drag::Edge && i == m_edgePiece) {   // the pulled edge sits under the pointer
            const double moved = double(m_edgeNow - m_edgeSource) / p.speed;
            (m_edgeEnd ? outEnd : outStart) = m_edgeOut + moved;
        }
        qreal x0 = xForOutput(outStart), x1 = xForOutput(outEnd);
        if (glide > 0) {
            x0 += m_ripple[i].first * glide;
            x1 += m_ripple[i].second * glide;
        }
        // Half the s1 gap from each side: neighbours stand 4 px apart, the axis unchanged.
        rects.append(QRectF(x0 + kGap / 2, track.top(), qMax<qreal>(0, x1 - x0 - kGap), track.height()));
    }
    return rects;
}

bool VideoTimeline::edgeAt(QPointF pos, int *piece, bool *end) const {
    const QRectF track = trackRect();
    if (m_duration <= 0 || pos.y() < track.top() || pos.y() > belowWaveform()) return false;
    const QVector<QRectF> rects = pieceRects();
    qreal best = kGripReach;
    bool found = false;
    for (int i = 0; i < rects.size(); ++i) {
        // A grip's middle sits kInset / 2 inside its block; at a seam the pointer's side decides.
        for (const bool isEnd : {false, true}) {
            const qreal edge = isEnd ? rects[i].right() : rects[i].left();
            if (edge < track.left() - kGap || edge > track.right() + kGap) continue;
            const qreal middle = isEnd ? edge - kInset / 2 : edge + kInset / 2;   // the grip as drawn
            const qreal distance = qAbs(pos.x() - middle);
            if (distance <= best) {
                best = distance;
                *piece = i;
                *end = isEnd;
                found = true;
            }
        }
    }
    return found;
}

int VideoTimeline::selectedPiece() const {
    if (m_selectedFragment < 0) return -1;
    const QVector<TimePiece> &pieces = m_axis.pieces();
    for (int i = 0; i < pieces.size(); ++i)
        if (fragments::indexAt(m_fragments, qint64(pieces[i].srcStart)) == m_selectedFragment) return i;
    return -1;
}

bool VideoTimeline::notchShown(int fragment) const {
    const QVector<Fragment> parts = fragments::expanded(m_fragments);
    if (fragment < 0 || fragment >= parts.size() || !parts[fragment].removed) return false;
    const double at = edited(parts[fragment].startMs);
    return parts[fragment].startMs > m_in && parts[fragment].startMs < m_out && at > 0 && at < editedDuration();
}

void VideoTimeline::setTrimRange(qint64 inMs, qint64 outMs) { setClip(inMs, outMs, m_fragments); }

void VideoTimeline::setFragments(const QVector<Fragment> &fragments) { setClip(m_in, m_out, fragments); }

void VideoTimeline::setClip(qint64 inMs, qint64 outMs, const QVector<Fragment> &fragments) {
    qint64 in = 0, out = 0;
    if (m_duration > 0) {
        const qint64 gap = qMin(m_minimumRange, m_duration);
        in = qBound<qint64>(0, inMs, m_duration - gap);
        out = qBound<qint64>(in + gap, outMs, m_duration);
    }
    if (in == m_in && out == m_out && fragments == m_fragments) return;
    // Where each source time is shown now, a running glide or drag included: the new
    // blocks start their glide from there.
    const QVector<TimePiece> before = m_axis.pieces();
    const QVector<QRectF> shown = pieceRects();
    const auto shownX = [&](double src, bool isEnd) -> qreal {
        for (int i = 0; i < before.size(); ++i) {
            const TimePiece &p = before[i];
            const bool inside = isEnd ? src > p.srcStart && src <= p.srcEnd : src >= p.srcStart && src < p.srcEnd;
            if (!inside) continue;
            const double at = (src - p.srcStart) / qMax(1.0, p.srcEnd - p.srcStart);
            const qreal x0 = shown[i].left() - kGap / 2, x1 = shown[i].right() + kGap / 2;
            return x0 + (x1 - x0) * at;
        }
        return xForOutput(m_axis.toOutputAfter(src));   // it was left out: where it would appear
    };
    QVector<std::pair<double, double>> sources;
    for (const TimePiece &p : TimeMap(m_duration, in, out, fragments).pieces()) sources.append({p.srcStart, p.srcEnd});
    QVector<std::pair<qreal, qreal>> from;
    for (const auto &[a, b] : std::as_const(sources)) from.append({shownX(a, false), shownX(b, true)});

    const bool fullView = m_viewStart == 0 && m_viewEnd == editedDuration();
    const qint64 sourceStart = source(m_viewStart), sourceEnd = source(m_viewEnd);   // the view keeps its stretch
    m_in = in;
    m_out = out;
    m_fragments = fragments;
    rebuildAxis();
    // Any edit of the pieces ends a mark: it was drawn against the old ones.
    m_markAnchor = m_markHead = -1;
    m_markOpen = false;
    m_markFade->jumpTo(0);
    if (m_selectedFragment >= fragments::expanded(m_fragments).size()) m_selectedFragment = -1;
    m_hoverCut = m_hoverEdge = -1;
    if (fullView) fitClip();
    else setViewRange(qRound64(edited(sourceStart)), qRound64(edited(sourceEnd)));

    m_ripple.clear();
    bool moves = false;
    const QVector<TimePiece> &now = m_axis.pieces();
    for (int i = 0; i < now.size() && i < from.size(); ++i) {
        const qreal dx0 = from[i].first - xForOutput(now[i].outStart), dx1 = from[i].second - xForOutput(now[i].outEnd());
        m_ripple.append({dx0, dx1});
        moves = moves || qAbs(dx0) > 0.5 || qAbs(dx1) > 0.5;
    }
    if (moves && isVisible() && Spring::enabled() && before.size() > 0) {
        m_rippleGlide->jumpTo(1);
        m_rippleGlide->setTarget(0);
    } else {
        m_ripple.clear();
        m_rippleGlide->jumpTo(0);
    }
    emit viewRangeChanged();
    update();
}

void VideoTimeline::beginEdgeDrag(int piece, bool end, qreal x, Qt::KeyboardModifiers modifiers) {
    const QVector<TimePiece> &pieces = m_axis.pieces();
    m_ripple.clear();   // a glide still running ends where it was going
    m_rippleGlide->jumpTo(0);
    m_drag = Drag::Edge;
    m_edgePiece = piece;
    m_edgeEnd = end;
    m_edgeSource = m_edgeNow = qRound64(end ? pieces[piece].srcEnd : pieces[piece].srcStart);
    m_edgeOut = end ? pieces[piece].outEnd() : pieces[piece].outStart;
    m_dragTime = m_edgeOut;
    m_dragX = m_pressX = m_pointerX = x;
    m_fine = modifiers.testFlag(Qt::ShiftModifier);
    m_modifiers = modifiers;
    m_clipBefore = m_clipPreview = clip();
    m_beforePosition = m_position;
}

void VideoTimeline::moveEdgeDrag(qreal x) {
    const TimePiece &piece = m_axis.pieces()[m_edgePiece];
    const bool fine = m_modifiers.testFlag(Qt::ShiftModifier);
    if (fine != m_fine) {   // the edge stays where it is; from here on the pointer moves it finer
        m_dragTime = m_edgeOut + double(m_edgeNow - m_edgeSource) / piece.speed;
        m_dragX = x;
        m_fine = fine;
    }
    const double msPerPx = double(m_viewEnd - m_viewStart) / qMax<qreal>(1, trackRect().width());
    const double pointerOut = m_dragTime + (x - m_dragX) * msPerPx * (m_fine ? 0.1 : 1.0);
    qint64 target = m_edgeSource + qRound64((pointerOut - m_edgeOut) * piece.speed);
    // The playhead from before the press holds the edge within 6 px.
    if (qAbs(double(target - m_beforePosition) / piece.speed / msPerPx) <= 6) target = m_beforePosition;
    const auto [lo, hi] = m_edgeEnd ? fragments::endRange(m_clipBefore, m_edgePiece, m_duration)
                                    : fragments::startRange(m_clipBefore, m_edgePiece, m_duration);
    m_edgeNow = std::clamp(target, lo, hi);
    m_clipPreview = m_edgeEnd ? fragments::moveEnd(m_clipBefore, m_edgePiece, m_edgeNow, m_duration)
                              : fragments::moveStart(m_clipBefore, m_edgePiece, m_edgeNow, m_duration);
    // The canvas shows the frame at the edge: the first one kept, or the last.
    m_position = m_edgeEnd ? qMax<qint64>(0, m_edgeNow - 1) : m_edgeNow;
    emit seekRequested(m_position);
    emit hoverRequested(m_position, QPoint(qRound(x), int(kTrackTop)));
    update();
}

int VideoTimeline::cutAt(QPointF pos) const {
    const QRectF track = trackRect();
    if (pos.y() > track.top() + 4) return -1;
    const QVector<Fragment> parts = fragments::expanded(m_fragments);
    for (int i = 0; i < parts.size(); ++i)
        if (parts[i].removed && notchShown(i) && qAbs(pos.x() - xForTime(parts[i].startMs)) <= 6) return i;
    return -1;
}

bool VideoTimeline::hasMark() const {
    const auto [from, to] = markRange();
    return to > from;
}

std::pair<qint64, qint64> VideoTimeline::markRange() const {
    if (m_markAnchor < 0 || m_markHead < 0) return {0, 0};
    const qint64 from = qBound(m_in, qMin(m_markAnchor, m_markHead), m_out);
    const qint64 to = qBound(m_in, qMax(m_markAnchor, m_markHead), m_out);
    if (to - from < m_minimumRange) return {0, 0};   // under a frame cuts nothing
    return {from, to};
}

void VideoTimeline::showMark() {
    m_markShown = markRange();
    m_markFade->jumpTo(1);   // a new mark shows at once; only clearing fades
}

void VideoTimeline::setMark(qint64 anchorMs, qint64 headMs) {
    m_markAnchor = qBound<qint64>(0, anchorMs, m_duration);
    m_markHead = qBound<qint64>(0, headMs, m_duration);
    m_markOpen = false;
    showMark();
    emit markChanged();
    update();
}

void VideoTimeline::markFromPlayhead() {
    m_markAnchor = m_markHead = m_position;
    m_markOpen = true;
    showMark();
    emit markChanged();
    update();
}

void VideoTimeline::markToPlayhead() {
    if (m_markAnchor < 0) return;
    m_markHead = m_position;
    m_markOpen = false;
    showMark();
    emit markChanged();
    update();
}

void VideoTimeline::clearMark() {
    if (m_markAnchor < 0 && !m_markOpen) return;
    m_markShown = markRange();
    m_markAnchor = m_markHead = -1;
    m_markOpen = false;
    if (isVisible()) m_markFade->setTarget(0); else m_markFade->jumpTo(0);
    emit markChanged();
    update();
}

// Ends snap to the trim, the clip's ends and the playhead from before the press within
// 6 px, and to seams within at least kMinFragmentMs, so no sliver is left beside one.
qint64 VideoTimeline::snapMark(qint64 timeMs) const {
    const qreal msPerPx = qreal(m_viewEnd - m_viewStart) / qMax<qreal>(1, trackRect().width());
    const qint64 reach = qRound64(6 * msPerPx);
    QVector<qint64> seams;
    for (const Fragment &f : fragments::expanded(m_fragments)) if (f.startMs > 0) seams.append(f.startMs);
    const qint64 onSeam = zoomlane::snap(timeMs, seams, qMax(reach, fragments::kMinFragmentMs));
    if (onSeam != timeMs) return onSeam;
    return zoomlane::snap(timeMs, {m_in, m_out, 0, m_duration, m_beforePosition}, reach);
}

}

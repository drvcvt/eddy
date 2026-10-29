// The clip model (K, docs/plans/2026-10/2026-10-27-clips.md): every kept piece is a block,
// every block edge a grip, and what is left out a ghost at its place (G,
// 2026-10-28-ghosts.md). Pulling an edge moves only that edge: the ghost beside it grows or
// shrinks, nothing else moves.
#include "timeline/videotimeline.h"
#include "app/spring.h"
#include "timeline/lanes/zoomlane.h"
#include <algorithm>

namespace eddy {

namespace {
// The source of `a` that `b` does not cover; both in source order, neither overlapping itself.
QVector<std::pair<qint64, qint64>> minus(const QVector<TimePiece> &a, const QVector<TimePiece> &b) {
    QVector<std::pair<qint64, qint64>> out;
    for (const TimePiece &p : a) {
        qint64 from = qint64(p.srcStart);
        const qint64 to = qint64(p.srcEnd);
        for (const TimePiece &q : b) {
            if (q.srcEnd <= from || q.srcStart >= to) continue;
            if (q.srcStart > from) out.append({from, qint64(q.srcStart)});
            from = std::max(from, qint64(q.srcEnd));
        }
        if (from < to) out.append({from, to});
    }
    return out;
}
}

qreal VideoTimeline::xForShown(double shownMs) const {
    const QRectF track = trackRect();
    if (m_viewEnd <= m_viewStart) return track.left();
    return track.left() + track.width() * (shownMs - m_viewStart) / (m_viewEnd - m_viewStart);
}

QVector<QRectF> VideoTimeline::pieceRects() const {
    const QRectF track = trackRect();
    const QVector<TimePiece> &pieces = m_axis.pieces();
    const qreal glide = m_ripple.size() == pieces.size() ? m_rippleGlide->value() : 0;
    QVector<QRectF> rects;
    for (int i = 0; i < pieces.size(); ++i) {
        const TimePiece &p = shownPiece(i);
        double start = p.outStart, end = p.outEnd();
        if (m_drag == Drag::Edge && i == m_edgePiece) {   // the pulled edge sits under the pointer
            const double moved = double(m_edgeNow - m_edgeSource) / p.speed;
            (m_edgeEnd ? end : start) = m_edgeOut + moved;
        }
        qreal x0 = xForShown(start), x1 = xForShown(end);
        if (m_drag == Drag::Move && i >= m_movePiece) {   // P: the moved piece and all after it
            const qreal dx = (m_moveNow - m_movePause) * trackRect().width() / qMax<qint64>(1, m_viewEnd - m_viewStart);
            x0 += dx;
            x1 += dx;
        }
        if (glide > 0) {
            x0 += m_ripple[i].first * glide;
            x1 += m_ripple[i].second * glide;
        }
        // Half the s1 gap from each side: neighbours stand 4 px apart, the axis unchanged.
        rects.append(QRectF(x0 + kGap / 2, track.top(), qMax<qreal>(0, x1 - x0 - kGap), track.height()));
    }
    return rects;
}

// It rides its piece's start: a pulled start or a moved piece takes it along.
QRectF VideoTimeline::pauseRect(int piece) const {
    const QVector<TimePiece> &pieces = m_axis.pieces();
    if (piece < 0 || piece >= pieces.size()) return {};
    const bool moving = m_drag == Drag::Move && piece == m_movePiece;
    const double pause = moving ? m_moveNow : pieces[piece].pauseBefore;
    if (pause <= 0) return {};
    const qreal right = pieceRects()[piece].left() - kGap;
    const qreal width = pause * trackRect().width() / qMax<qint64>(1, m_viewEnd - m_viewStart) - kGap;
    if (width <= 0) return {};
    return QRectF(right - width, trackRect().top(), width, trackRect().height());
}

QVector<QRectF> VideoTimeline::ghostRects() const {
    const QVector<QRectF> rects = pieceRects();
    QVector<QRectF> ghosts;
    if (rects.isEmpty()) return ghosts;
    const QRectF track = trackRect();
    const auto add = [&](qreal left, qreal right) {
        if (right - left >= 1) ghosts.append(QRectF(left, track.top(), right - left, track.height()));
    };
    qreal left = xForShown(0) + kGap / 2;
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF pause = pauseRect(i);
        add(left, (pause.isEmpty() ? rects[i].left() : pause.left()) - kGap);
        left = rects[i].right() + kGap;
    }
    // The last ghost rides the last block while it is moved or glides; pulled, it gives way.
    qreal shift = 0;
    if (m_drag == Drag::Move)
        shift = (m_moveNow - m_movePause) * track.width() / qMax<qint64>(1, m_viewEnd - m_viewStart);
    if (m_ripple.size() == rects.size()) shift += m_ripple.last().second * m_rippleGlide->value();
    add(left, xForShown(shownDuration()) - kGap / 2 + shift);
    return ghosts;
}

std::optional<std::pair<qint64, qint64>> VideoTimeline::ghostAt(QPointF pos) const {
    const QRectF track = trackRect();
    if (m_drag != Drag::None || pos.y() < track.top() || pos.y() > belowWaveform()) return std::nullopt;
    const QVector<QRectF> ghosts = ghostRects();
    if (std::none_of(ghosts.cbegin(), ghosts.cend(), [&](const QRectF &g) { return pos.x() >= g.left() && pos.x() <= g.right(); }))
        return std::nullopt;
    // The source gap between the kept pieces around it.
    const qint64 at = timeForX(pos.x());
    qint64 from = 0, to = m_duration;
    for (const TimePiece &p : m_axis.pieces()) {
        if (p.srcEnd <= at) from = qint64(p.srcEnd);
        else if (p.srcStart > at) { to = qint64(p.srcStart); break; }
        else return std::nullopt;   // inside a kept piece after all
    }
    if (to <= from) return std::nullopt;
    return std::pair{from, to};
}

void VideoTimeline::restoreGhost(std::pair<qint64, qint64> range) {
    const fragments::Clip before = clip();
    const fragments::Clip after = fragments::restore(before, range.first, range.second, m_duration);
    if (after == before) return;
    setClip(after.inMs, after.outMs, after.list);
    int piece = -1;   // the piece that now plays its start
    const QVector<TimePiece> pieces = m_axis.pieces();
    for (int i = 0; i < pieces.size(); ++i)
        if (pieces[i].srcStart <= range.first && pieces[i].srcEnd > range.first) piece = i;
    emit clipEdited(before, after, piece);
}

int VideoTimeline::pieceAt(QPointF pos) const {
    const QRectF track = trackRect();
    if (pos.y() < track.top() || pos.y() > belowWaveform()) return -1;
    const QVector<QRectF> rects = pieceRects();
    for (int i = 0; i < rects.size(); ++i)
        if (pos.x() >= rects[i].left() && pos.x() <= rects[i].right()) return i;
    return -1;
}

int VideoTimeline::pauseAt(QPointF pos) const {
    const QRectF track = trackRect();
    if (pos.y() < track.top() || pos.y() > belowWaveform()) return -1;
    for (int i = 0; i < m_axis.pieces().size(); ++i) {
        const QRectF pause = pauseRect(i).adjusted(-kGap, 0, kGap, 0);   // its gaps count as the pause
        if (!pause.isEmpty() && pos.x() >= pause.left() && pos.x() <= pause.right()) return i;
    }
    return -1;
}

void VideoTimeline::setPausePosition(double outMs) {
    // Only the playhead moves, as in setPosition: its old and new column.
    const qreal was = playheadX();
    m_position = qRound64(m_axis.toSource(outMs));
    m_pauseHead = m_axis.pauseAt(outMs) ? outMs : -1;
    for (const qreal x : {was, playheadX()}) update(QRectF(x - 5, 0, 10, height()).toAlignedRect());
}

qreal VideoTimeline::playheadX() const {
    return m_pauseHead >= 0 ? xForShown(shownForOutput(m_pauseHead)) : xForTime(m_position);
}

// P: Shift on a block's body moves it, and everything after it, along the output: the pause
// before it grows or shrinks; nothing passes its left neighbour.
void VideoTimeline::beginMove(int piece, qreal x) {
    m_ripple.clear();
    m_rippleGlide->jumpTo(0);
    m_drag = Drag::Move;
    m_movePiece = piece;
    m_movePause = m_moveNow = m_axis.pieces()[piece].pauseBefore;
    m_dragX = m_pressX = m_pointerX = x;
    m_dragTime = 0;   // what an edge pan has scrolled since the press
    m_clipBefore = m_clipPreview = clip();
    m_beforePosition = m_position;
}

void VideoTimeline::moveMove(qreal x) {
    const double msPerPx = double(m_viewEnd - m_viewStart) / qMax<qreal>(1, trackRect().width());
    double pause = m_movePause + (x - m_dragX) * msPerPx + m_dragTime;
    if (!m_modifiers.testFlag(Qt::ControlModifier)) {   // Ctrl places it freely
        // Back onto its neighbour within 8 px, or its start onto the playhead from before the press.
        const double onPlayhead = m_movePause + (shown(m_beforePosition) - shownPiece(m_movePiece).outStart);
        if (pause / msPerPx <= 8) pause = 0;
        else if (onPlayhead > 0 && qAbs(pause - onPlayhead) / msPerPx <= 8) pause = onPlayhead;
    }
    m_moveNow = std::clamp<double>(pause, 0, double(Fragment::kMaxPauseMs));
    m_clipPreview = fragments::setPause(m_clipBefore, m_movePiece, qRound64(m_moveNow), m_duration);
    update();
}

void VideoTimeline::editPause(int piece, std::optional<qint64> pauseMs, std::optional<bool> black) {
    const fragments::Clip before = clip();
    fragments::Clip after = before;
    if (pauseMs) after = fragments::setPause(after, piece, *pauseMs, m_duration);
    if (black) after = fragments::setPauseBlack(after, piece, *black, m_duration);
    if (after == before) return;
    setClip(after.inMs, after.outMs, after.list);   // glides from where it stands
    emit clipEdited(before, after, piece);
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
    const QVector<QRectF> drawn = pieceRects();
    const auto shownX = [&](double src, bool isEnd) -> qreal {
        for (int i = 0; i < before.size(); ++i) {
            const TimePiece &p = before[i];
            const bool inside = isEnd ? src > p.srcStart && src <= p.srcEnd : src >= p.srcStart && src < p.srcEnd;
            if (!inside) continue;
            const double at = (src - p.srcStart) / qMax(1.0, p.srcEnd - p.srcStart);
            const qreal x0 = drawn[i].left() - kGap / 2, x1 = drawn[i].right() + kGap / 2;
            return x0 + (x1 - x0) * at;
        }
        return xForShown(shown(qint64(src)));   // it was a ghost: where that stood
    };
    QVector<std::pair<double, double>> sources;
    const TimeMap next(m_duration, in, out, fragments);   // named: pieces() is a reference into it
    for (const TimePiece &p : next.pieces()) sources.append({p.srcStart, p.srcEnd});
    QVector<std::pair<qreal, qreal>> from;
    for (const auto &[a, b] : std::as_const(sources)) from.append({shownX(a, false), shownX(b, true)});

    const bool fullView = m_viewStart == 0 && m_viewEnd == shownDuration();
    const qint64 sourceStart = sourceAt(m_viewStart), sourceEnd = sourceAt(m_viewEnd);   // the view keeps its stretch
    m_in = in;
    m_out = out;
    m_fragments = fragments;
    rebuildAxis();
    // A pull showed its ghost all along; any other edit changes look where it cut or took back.
    m_freshCut.clear();
    m_freshKept.clear();
    if (m_drag == Drag::None && isVisible() && Spring::enabled() && !before.isEmpty()) {
        m_freshCut = minus(before, m_axis.pieces());
        m_freshKept = minus(m_axis.pieces(), before);
    }
    if (m_freshCut.isEmpty() && m_freshKept.isEmpty()) m_fresh->jumpTo(0);
    else { m_fresh->jumpTo(1); m_fresh->setTarget(0); }
    // Any edit of the pieces ends a mark: it was drawn against the old ones.
    m_markAnchor = m_markHead = -1;
    m_markOpen = false;
    m_markFade->jumpTo(0);
    if (m_selectedFragment >= fragments::expanded(m_fragments).size()) m_selectedFragment = -1;
    m_hoverGhost = m_hoverEdge = -1;
    if (fullView) fitClip();
    else setViewRange(qRound64(shown(sourceStart)), qRound64(shown(sourceEnd)));

    glideFrom(from, !before.isEmpty());
    emit viewRangeChanged();
    update();
}

void VideoTimeline::glideFrom(const QVector<std::pair<qreal, qreal>> &from, bool animate) {
    m_ripple.clear();
    bool moves = false;
    const QVector<TimePiece> &now = m_axis.pieces();
    for (int i = 0; i < now.size() && i < from.size(); ++i) {
        const qreal dx0 = from[i].first - xForShown(shownPiece(i).outStart), dx1 = from[i].second - xForShown(shownPiece(i).outEnd());
        m_ripple.append({dx0, dx1});
        moves = moves || qAbs(dx0) > 0.5 || qAbs(dx1) > 0.5;
    }
    if (moves && animate && isVisible() && Spring::enabled()) {
        m_rippleGlide->jumpTo(1);
        m_rippleGlide->setTarget(0);
    } else {
        m_ripple.clear();
        m_rippleGlide->jumpTo(0);
    }
}

bool VideoTimeline::gapClosed(qint64 fromMs, qint64 toMs) const {
    return std::any_of(m_closed.cbegin(), m_closed.cend(), [&](qint64 at) { return at >= fromMs && at < toMs; });
}

// A closed gap is the seam where it was: between two blocks, or at the strip's first or last edge.
std::optional<std::pair<qint64, qint64>> VideoTimeline::closedAt(QPointF pos) const {
    const QRectF track = trackRect();
    if (m_drag != Drag::None || pos.y() < track.top() || pos.y() > belowWaveform()) return std::nullopt;
    const QVector<TimePiece> &pieces = m_axis.pieces();
    const QVector<QRectF> rects = pieceRects();
    for (int i = 0; i <= pieces.size() && !rects.isEmpty(); ++i) {   // the gap before piece i, or after the last
        const qint64 from = i == 0 ? 0 : qint64(pieces[i - 1].srcEnd);
        const qint64 to = i == pieces.size() ? m_duration : qint64(pieces[i].srcStart);
        if (to <= from || !gapClosed(from, to)) continue;
        const QRectF pause = i < pieces.size() ? pauseRect(i) : QRectF();
        const qreal right = i == pieces.size() ? rects.last().right() : pause.isEmpty() ? rects[i].left() : pause.left();
        const qreal seam = i == 0 ? right : i == pieces.size() ? right : (rects[i - 1].right() + right) / 2;
        if (qAbs(pos.x() - seam) <= kGripReach) return std::pair{from, to};
    }
    return std::nullopt;
}

void VideoTimeline::setGapClosed(std::pair<qint64, qint64> range, bool closed) {
    if (gapClosed(range.first, range.second) == closed) return;
    QVector<std::pair<qreal, qreal>> from;
    for (const QRectF &r : pieceRects()) from.append({r.left() - kGap / 2, r.right() + kGap / 2});
    const bool fullView = m_viewStart == 0 && m_viewEnd == shownDuration();
    const qint64 sourceStart = sourceAt(m_viewStart), sourceEnd = sourceAt(m_viewEnd);
    m_closed.removeIf([&](qint64 at) { return at >= range.first && at < range.second; });
    if (closed) m_closed.append((range.first + range.second) / 2);
    rebuildAxis();
    m_hoverGhost = m_hoverEdge = -1;
    if (fullView) fitClip();
    else setViewRange(qRound64(shown(sourceStart)), qRound64(shown(sourceEnd)));
    glideFrom(from, true);
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
    m_edgeOut = end ? shownPiece(piece).outEnd() : shownPiece(piece).outStart;
    m_dragTime = m_edgeOut;
    m_dragX = m_pressX = m_pointerX = x;
    m_fine = modifiers.testFlag(Qt::AltModifier);   // G: Shift there opens a pause now
    m_modifiers = modifiers;
    m_clipBefore = m_clipPreview = clip();
    m_beforePosition = m_position;
}

void VideoTimeline::moveEdgeDrag(qreal x) {
    const TimePiece &piece = m_axis.pieces()[m_edgePiece];
    const bool fine = m_modifiers.testFlag(Qt::AltModifier);
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
    auto [lo, hi] = m_edgeEnd ? fragments::endRange(m_clipBefore, m_edgePiece, m_duration)
                              : fragments::startRange(m_clipBefore, m_edgePiece, m_duration);
    // Into a closed gap it holds at the seam: blocks never overlap. Opened, it takes back.
    const QVector<TimePiece> &all = m_axis.pieces();
    if (m_edgeEnd && gapClosed(m_edgeSource, m_edgePiece + 1 < all.size() ? qint64(all[m_edgePiece + 1].srcStart) : m_duration))
        hi = m_edgeSource;
    if (!m_edgeEnd && gapClosed(m_edgePiece > 0 ? qint64(all[m_edgePiece - 1].srcEnd) : 0, m_edgeSource))
        lo = m_edgeSource;
    m_edgeNow = std::clamp(target, lo, hi);
    m_clipPreview = m_edgeEnd ? fragments::moveEnd(m_clipBefore, m_edgePiece, m_edgeNow, m_duration)
                              : fragments::moveStart(m_clipBefore, m_edgePiece, m_edgeNow, m_duration);
    // The canvas shows the frame at the edge: the first one kept, or the last.
    m_position = m_edgeEnd ? qMax<qint64>(0, m_edgeNow - 1) : m_edgeNow;
    emit seekRequested(m_position);
    emit hoverRequested(m_position, QPoint(qRound(x), int(kTrackTop)));
    update();
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

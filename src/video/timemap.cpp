#include "video/timemap.h"
#include <algorithm>
#include <cmath>

namespace eddy {

TimeMap::TimeMap(qint64 durationMs, qint64 trimInMs, qint64 trimOutMs,
                 const QVector<Fragment> &fragments) {
    const QVector<Fragment> parts = fragments.isEmpty() ? QVector<Fragment>{Fragment()} : fragments;
    const double in = std::clamp<double>(trimInMs, 0, durationMs);
    const double out = std::clamp<double>(trimOutMs, in, durationMs);
    double at = 0;
    for (int i = 0; i < parts.size(); ++i) {
        const double start = std::max<double>(parts[i].startMs, in);
        const double end = std::min<double>(i + 1 < parts.size() ? parts[i + 1].startMs : durationMs, out);
        if (parts[i].removed || end <= start) continue;
        m_pieces.append({start, end, at, parts[i].speed});
        at = m_pieces.last().outEnd();
    }
}

std::optional<double> TimeMap::toOutput(double srcMs) const {
    for (int i = 0; i < m_pieces.size(); ++i) {
        const TimePiece &p = m_pieces[i];
        const bool last = i == m_pieces.size() - 1;
        if (srcMs >= p.srcStart && (srcMs < p.srcEnd || (last && srcMs == p.srcEnd)))
            return p.outStart + (srcMs - p.srcStart) / p.speed;
    }
    return std::nullopt;
}

double TimeMap::toOutputAfter(double srcMs) const {
    for (const TimePiece &p : m_pieces) {
        if (srcMs < p.srcStart) return p.outStart;
        if (srcMs < p.srcEnd) return p.outStart + (srcMs - p.srcStart) / p.speed;
    }
    return outputDurationMs();
}

double TimeMap::toSource(double outMs) const {
    if (m_pieces.isEmpty()) return 0;
    const double t = std::clamp(outMs, 0.0, outputDurationMs());
    // The last piece starting at or before t, so a seam belongs to the later piece.
    const auto after = std::upper_bound(m_pieces.cbegin(), m_pieces.cend(), t,
        [](double value, const TimePiece &p) { return value < p.outStart; });
    const TimePiece &p = *(after == m_pieces.cbegin() ? after : after - 1);
    return std::min(p.srcEnd, p.srcStart + (t - p.outStart) * p.speed);
}

namespace {
double gridFps(double fps) { return fps > 0 ? fps : 30.0; }
qint64 msOf(qint64 index, double fps) { return qRound64(index * 1000.0 / fps); }

// A grid time that is not in a kept piece moves out of the cut it is in.
qint64 outOfCut(const TimeMap &clip, qint64 ms, double fps, int direction, qint64 last) {
    if (clip.toOutput(double(ms)) || clip.pieces().isEmpty()) return ms;
    const QVector<TimePiece> &pieces = clip.pieces();
    const TimePiece *before = nullptr, *after = nullptr;
    for (const TimePiece &p : pieces) {
        if (p.srcEnd <= ms) before = &p;
        else if (!after && p.srcStart > ms) after = &p;
    }
    if (direction < 0 && !before) direction = 1;   // nothing kept before: go forwards
    if (direction > 0 && !after) direction = -1;
    if (direction > 0) return std::min(last, msOf(qint64(std::ceil(after->srcStart * fps / 1000.0 - 1e-9)), fps));
    return std::max<qint64>(0, msOf(qint64(std::ceil(before->srcEnd * fps / 1000.0 - 1e-9)) - 1, fps));
}
}

qint64 stepFrames(const TimeMap &clip, qint64 durationMs, double fps, qint64 sourceMs, int frames) {
    fps = gridFps(fps);
    const qint64 last = std::max<qint64>(0, durationMs - 1);
    const qint64 index = qRound64(sourceMs * fps / 1000.0) + frames;
    const qint64 ms = std::clamp<qint64>(msOf(index, fps), 0, last);
    return outOfCut(clip, ms, fps, frames < 0 ? -1 : 1, last);
}

qint64 stepOutput(const TimeMap &clip, qint64 durationMs, double fps, qint64 sourceMs, double outputDeltaMs) {
    fps = gridFps(fps);
    const qint64 last = std::max<qint64>(0, durationMs - 1);
    if (clip.pieces().isEmpty()) return std::clamp<qint64>(sourceMs, 0, last);
    const double target = clip.toSource(clip.toOutputAfter(double(sourceMs)) + outputDeltaMs);
    const qint64 ms = std::clamp<qint64>(msOf(qRound64(target * fps / 1000.0), fps), 0, last);
    return outOfCut(clip, ms, fps, outputDeltaMs < 0 ? -1 : 1, last);
}

std::optional<qint64> nextEdge(const TimeMap &clip, const QVector<qint64> &edges, qint64 sourceMs, int direction) {
    const double here = clip.toOutputAfter(double(sourceMs));
    std::optional<double> best;
    for (const qint64 edge : edges) {
        const double at = clip.toOutputAfter(double(edge));
        if (direction > 0 ? at <= here + 0.5 : at >= here - 0.5) continue;
        if (!best || (direction > 0 ? at < *best : at > *best)) best = at;
    }
    if (!best) return std::nullopt;
    return qRound64(clip.toSource(*best));
}

}

#include "timemap.h"
#include <algorithm>

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

}

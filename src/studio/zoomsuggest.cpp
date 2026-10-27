#include "studio/zoomsuggest.h"
#include <QLineF>
#include <algorithm>
#include <cmath>

namespace eddy {

namespace {
constexpr qint64 kMinRestMs = 500, kMaxRestMs = 2600;
constexpr qint64 kRestLeadMs = 400, kRestZoomMs = 2000;
constexpr qint64 kClickGapMs = 1500, kClickLeadMs = 600, kClickTailMs = 1200;
constexpr qint64 kSpacingMs = 1800;

struct Candidate {
    qint64 start, end;    // the segment
    qint64 fromMs, toMs;  // what it is about: the rest or the clicks
    QPointF point;
    double scale;
};
}

QVector<ZoomSegment> suggestZooms(const CursorTrack &track, const TimeMap &time, qint64 durationMs,
                                  const QVector<ZoomSegment> &existing, quint32 firstId,
                                  ZoomSegment::Motion motion) {
    QVector<QPair<qint64, qint64>> taken;
    for (const ZoomSegment &z : existing) taken.append({z.startMs, z.endMs});
    auto fits = [&](const Candidate &c) {
        if (!time.toOutput(double(c.fromMs)) || !time.toOutput(double(c.toMs - 1))) return false;
        for (const auto &[a, b] : taken)
            if (c.start < b + kSpacingMs && a < c.end + kSpacingMs) return false;
        return true;
    };
    auto segment = [&](qint64 lead, qint64 from, qint64 to, qint64 length, QPointF point, double scale) {
        const qint64 start = std::clamp<qint64>(from - lead, 0, durationMs);
        const qint64 end = std::min(durationMs, length > 0 ? start + length : to);
        return Candidate{start, end, from, to, point, scale};
    };
    QVector<Candidate> chosen;
    auto take = [&](const Candidate &c) {
        if (c.end - c.start < kMinRestMs || !fits(c)) return;
        chosen.append(c);
        taken.append({c.start, c.end});
    };

    // Left clicks within 1.5 s of each other are one group.
    QVector<qint64> clicks;
    for (const CursorClick &c : track.clicks)
        if (c.button == 1 && c.down) clicks.append(c.ms);
    std::sort(clicks.begin(), clicks.end());
    for (int i = 0; i < clicks.size();) {
        int j = i + 1;
        while (j < clicks.size() && clicks[j] - clicks[j - 1] <= kClickGapMs) ++j;
        QPointF sum;
        int n = 0;
        for (int k = i; k < j; ++k)
            if (const auto p = track.positionAt(clicks[k])) { sum += *p; ++n; }
        if (n > 0) {
            Candidate c = segment(kClickLeadMs, clicks[i], clicks[j - 1] + kClickTailMs, 0, sum / n, 2.0);
            c.toMs = clicks[j - 1] + 1;
            take(c);
        }
        i = j;
    }

    // Rests: the pointer stays within 1.5 % of the diagonal. A gap between
    // samples is a rest too, since Boltsnap only records moves.
    const double radius = 0.015 * std::hypot(track.videoSize.width(), track.videoSize.height());
    QVector<Candidate> rests;
    const QVector<CursorSample> &s = track.samples;
    for (int i = 0; i < s.size();) {
        if (!s[i].visible) { ++i; continue; }
        int j = i + 1;
        QPointF sum = s[i].pos;
        while (j < s.size() && s[j].visible && QLineF(s[j].pos, s[i].pos).length() <= radius) sum += s[j++].pos;
        const qint64 end = j < s.size() ? s[j].ms : std::min(durationMs, s.last().ms);
        if (end - s[i].ms >= kMinRestMs && end - s[i].ms <= kMaxRestMs)
            rests.append(segment(kRestLeadMs, s[i].ms, end, kRestZoomMs, sum / (j - i), 1.8));
        i = j;
    }
    std::stable_sort(rests.begin(), rests.end(), [](const Candidate &a, const Candidate &b) {
        return a.toMs - a.fromMs > b.toMs - b.fromMs;
    });
    for (const Candidate &c : rests) take(c);

    std::sort(chosen.begin(), chosen.end(), [](const Candidate &a, const Candidate &b) { return a.start < b.start; });
    QVector<ZoomSegment> out;
    for (const Candidate &c : chosen) {
        ZoomSegment z;
        z.id = firstId++;
        z.startMs = c.start;
        z.endMs = c.end;
        z.scale = c.scale;
        z.point = c.point;
        z.motion = motion;
        out.append(z);
    }
    return out;
}

namespace {
constexpr qint64 kWindowMs = 50, kMinPauseMs = 1000, kPadMs = 200, kMoveGuardMs = 100;

// Overlap of two sorted span lists.
QVector<TimeSpan> intersect(const QVector<TimeSpan> &a, const QVector<TimeSpan> &b) {
    QVector<TimeSpan> out;
    for (int i = 0, j = 0; i < a.size() && j < b.size();) {
        const qint64 from = std::max(a[i].first, b[j].first), to = std::min(a[i].second, b[j].second);
        if (from < to) out.append({from, to});
        (a[i].second < b[j].second) ? ++i : ++j;
    }
    return out;
}

// Runs of quiet 50 ms windows over the kept 1x pieces.
QVector<TimeSpan> silences(const PauseSignals &s, const QVector<TimeSpan> &pieces) {
    QVector<std::pair<qint64, double>> windows;   // start, dBFS
    for (const auto &[from, to] : pieces)
        for (qint64 t = from; t + kWindowMs <= to; t += kWindowMs) {
            const float peak = s.peak(t, t + kWindowMs);
            windows.append({t, peak > 1e-6f ? 20 * std::log10(double(peak)) : -120.0});
        }
    if (windows.isEmpty()) return {};
    QVector<double> levels;
    for (const auto &w : windows) levels.append(w.second);
    std::sort(levels.begin(), levels.end());
    const double floor = levels[levels.size() / 10];   // the 10th percentile: the room's noise
    const double threshold = std::clamp(floor + 10.0, -55.0, -35.0);
    QVector<TimeSpan> out;
    for (const auto &[t, level] : windows) {
        if (level > threshold) continue;
        if (!out.isEmpty() && out.last().second == t) out.last().second = t + kWindowMs;
        else out.append({t, t + kWindowMs});
    }
    return out;
}

// Where the pointer does nothing: a move past 0.5 % of the diagonal or a click blocks
// 100 ms around it. A hidden pointer is still.
QVector<TimeSpan> stillness(const CursorTrack &track, qint64 from, qint64 to) {
    const double reach = 0.005 * std::hypot(track.videoSize.width(), track.videoSize.height());
    QVector<qint64> events;
    std::optional<QPointF> anchor;
    for (const CursorSample &s : track.samples) {
        if (!s.visible) continue;
        if (!anchor || QLineF(*anchor, s.pos).length() > reach) {
            if (anchor) events.append(s.ms);
            anchor = s.pos;
        }
    }
    for (const CursorClick &c : track.clicks) events.append(c.ms);
    std::sort(events.begin(), events.end());
    QVector<TimeSpan> out;
    qint64 at = from;
    for (const qint64 e : std::as_const(events)) {
        const qint64 blockFrom = e - kMoveGuardMs, blockTo = e + kMoveGuardMs;
        if (blockFrom > at) out.append({at, std::min(blockFrom, to)});
        at = std::max(at, blockTo);
        if (at >= to) break;
    }
    if (at < to) out.append({at, to});
    return out;
}
}

QVector<TimeSpan> findPauses(const PauseSignals &s) {
    // Only kept pieces at 1x: cut ones are gone, sped ones were chosen by hand.
    QVector<TimeSpan> pieces;
    for (const TimePiece &p : s.time.pieces())
        if (p.speed == 1.0) pieces.append({qint64(std::ceil(p.srcStart)), qint64(std::floor(p.srcEnd))});
    if (pieces.isEmpty()) return {};
    const qint64 rangeFrom = pieces.first().first, rangeTo = pieces.last().second;
    QVector<TimeSpan> quiet = pieces;
    bool any = false;
    if (s.peak) { quiet = intersect(quiet, silences(s, pieces)); any = true; }
    if (s.pointer && !s.pointer->samples.isEmpty()) {
        quiet = intersect(quiet, stillness(*s.pointer, rangeFrom, rangeTo));
        any = true;
    }
    // A frozen picture backs a missing signal; with neither sound nor pointer it alone decides.
    if (s.frozen && (!s.peak || !s.pointer || s.pointer->samples.isEmpty())) {
        QVector<TimeSpan> frozen = *s.frozen;
        std::sort(frozen.begin(), frozen.end());
        quiet = intersect(quiet, frozen);
        any = true;
    }
    if (!any) return {};
    QVector<TimeSpan> out;
    for (auto [from, to] : quiet) {
        if (to - from < kMinPauseMs) continue;
        if (from > rangeFrom) from += kPadMs;   // a breath before and after, none at the range's ends
        if (to < rangeTo) to -= kPadMs;
        if (to > from) out.append({from, to});
    }
    return out;
}

}

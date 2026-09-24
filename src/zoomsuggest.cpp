#include "zoomsuggest.h"
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

}

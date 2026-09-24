#include "zoomlane.h"
#include <QtGlobal>
#include <algorithm>

namespace eddy::zoomlane {

std::optional<std::pair<qint64, qint64>> placeNew(const QVector<ZoomSegment> &zooms, qint64 timeMs,
                                                  qint64 durationMs) {
    qint64 lo = 0, hi = durationMs;
    for (const ZoomSegment &z : zooms) {
        if (z.startMs <= timeMs && timeMs < z.endMs) return std::nullopt;
        if (z.endMs <= timeMs) lo = std::max(lo, z.endMs);
        else hi = std::min(hi, z.startMs);
    }
    if (hi - lo < kMinZoomMs) return std::nullopt;
    const qint64 start = std::max(lo, std::min(timeMs, hi - kNewZoomMs));
    return std::pair{start, std::min(hi, start + kNewZoomMs)};
}

int insert(QVector<ZoomSegment> &zooms, const ZoomSegment &zoom) {
    const auto at = std::upper_bound(zooms.begin(), zooms.end(), zoom.startMs,
        [](qint64 start, const ZoomSegment &z) { return start < z.startMs; });
    const int index = int(at - zooms.begin());
    zooms.insert(index, zoom);
    return index;
}

quint32 nextId(const QVector<ZoomSegment> &zooms) {
    quint32 id = 0;
    for (const ZoomSegment &z : zooms) id = std::max(id, z.id);
    return id + 1;
}

int indexOf(const QVector<ZoomSegment> &zooms, quint32 id) {
    for (int i = 0; i < zooms.size(); ++i)
        if (zooms[i].id == id) return i;
    return -1;
}

void move(QVector<ZoomSegment> &zooms, quint32 id, qint64 startMs, qint64 durationMs) {
    const int i = indexOf(zooms, id);
    if (i < 0) return;
    const qint64 length = zooms[i].endMs - zooms[i].startMs;
    const qint64 lo = i > 0 ? zooms[i - 1].endMs : 0;
    const qint64 hi = i + 1 < zooms.size() ? zooms[i + 1].startMs : durationMs;
    zooms[i].startMs = qBound(lo, startMs, hi - length);
    zooms[i].endMs = zooms[i].startMs + length;
}

void resize(QVector<ZoomSegment> &zooms, quint32 id, bool startEdge, qint64 timeMs, qint64 durationMs) {
    const int i = indexOf(zooms, id);
    if (i < 0) return;
    ZoomSegment &z = zooms[i];
    if (startEdge)
        z.startMs = qBound(i > 0 ? zooms[i - 1].endMs : 0, timeMs, z.endMs - kMinZoomMs);
    else
        z.endMs = qBound(z.startMs + kMinZoomMs, timeMs,
                         i + 1 < zooms.size() ? zooms[i + 1].startMs : durationMs);
}

qint64 snap(qint64 timeMs, const QVector<qint64> &anchors, qint64 toleranceMs) {
    qint64 best = timeMs, distance = toleranceMs + 1;
    for (qint64 anchor : anchors) {
        const qint64 d = qAbs(anchor - timeMs);
        if (d <= toleranceMs && d < distance) {
            best = anchor;
            distance = d;
        }
    }
    return best;
}

}

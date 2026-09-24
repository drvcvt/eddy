#pragma once
#include <QVector>
#include <optional>
#include <utility>
#include "studiodocument.h"

// Editing zooms on their lane (studio plan 6.1): pure functions over the
// sorted, never overlapping list, so the timeline only paints and forwards.
namespace eddy::zoomlane {

inline constexpr qint64 kNewZoomMs = 2000;
inline constexpr qint64 kMinZoomMs = 500;

// Where a new zoom at `timeMs` goes: two seconds, moved back into the free
// gap around it, shorter only when the gap is; nothing inside a zoom or when
// the gap is under half a second.
std::optional<std::pair<qint64, qint64>> placeNew(const QVector<ZoomSegment> &zooms, qint64 timeMs,
                                                  qint64 durationMs);
// Inserts in start order and returns the index.
int insert(QVector<ZoomSegment> &zooms, const ZoomSegment &zoom);
quint32 nextId(const QVector<ZoomSegment> &zooms);
int indexOf(const QVector<ZoomSegment> &zooms, quint32 id);
// Body drag: keeps the length, stops at the neighbours and the clip's ends.
void move(QVector<ZoomSegment> &zooms, quint32 id, qint64 startMs, qint64 durationMs);
// Edge drag: at least kMinZoomMs long, never over a neighbour or past the ends.
void resize(QVector<ZoomSegment> &zooms, quint32 id, bool startEdge, qint64 timeMs, qint64 durationMs);
// The nearest anchor within `toleranceMs`, else `timeMs`.
qint64 snap(qint64 timeMs, const QVector<qint64> &anchors, qint64 toleranceMs);

}

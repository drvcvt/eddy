#pragma once
#include <QVector>
#include "cursortrack.h"
#include "studiodocument.h"
#include "timemap.h"

namespace eddy {

// "Suggest zooms" (studio plan 6.3, N5 = a): normal zoom segments where the
// pointer clicks or rests. Click groups come first; rests of 0.5 to 2.6 s
// within 1.5 % of the diagonal fill the gaps. Every suggestion keeps 1.8 s
// from existing zooms and from each other and lies in kept fragments.
QVector<ZoomSegment> suggestZooms(const CursorTrack &track, const TimeMap &time, qint64 durationMs,
                                  const QVector<ZoomSegment> &existing, quint32 firstId,
                                  ZoomSegment::Motion motion);

}

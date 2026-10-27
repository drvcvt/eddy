#pragma once
#include <QVector>
#include <functional>
#include <optional>
#include "video/cursortrack.h"
#include "studio/studiodocument.h"
#include "video/timemap.h"

namespace eddy {

// "Suggest zooms" (studio plan 6.3, N5 = a): normal zoom segments where the
// pointer clicks or rests. Click groups come first; rests of 0.5 to 2.6 s
// within 1.5 % of the diagonal fill the gaps. Every suggestion keeps 1.8 s
// from existing zooms and from each other and lies in kept fragments.
QVector<ZoomSegment> suggestZooms(const CursorTrack &track, const TimeMap &time, qint64 durationMs,
                                  const QVector<ZoomSegment> &existing, quint32 firstId,
                                  ZoomSegment::Motion motion);

// "Cut pauses" (C2): stretches where nothing happens, from every signal there is.
using TimeSpan = std::pair<qint64, qint64>;   // source ms, end exclusive
struct PauseSignals {
    TimeMap time;                    // the trim and fragments: pauses lie in kept 1x pieces
    // The loudest sample over [from, to), 0..1; empty without usable sound.
    std::function<float(qint64, qint64)> peak;
    const CursorTrack *pointer = nullptr;      // empty without a recorded pointer
    std::optional<QVector<TimeSpan>> frozen;   // ffmpeg freezedetect, when it ran
};
// Silence (50 ms windows under the noise floor + 10 dB, held to -55..-35 dBFS), a still
// pointer (no move over 0.5 % of the diagonal, no click, ±100 ms) and a frozen picture,
// each where available, must all agree. Pauses of 1 s or more, 200 ms kept at each side.
QVector<TimeSpan> findPauses(const PauseSignals &input);

}

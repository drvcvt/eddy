#pragma once
#include <QVector>
#include <optional>
#include "studio/studiodocument.h"

namespace eddy {

// One kept stretch of the source and where it lands in the output.
struct TimePiece {
    double srcStart = 0, srcEnd = 0;   // source ms, end exclusive
    double outStart = 0;               // output ms
    double speed = 1;
    double pauseBefore = 0;            // P: output ms of pause right before outStart
    bool pauseBlack = false;           // ... shown black instead of the frame before held
    double outEnd() const { return outStart + (srcEnd - srcStart) / speed; }
    double pauseStart() const { return outStart - pauseBefore; }
};

// Source time <-> output time for the trim plus fragments (studio plan 3.3).
// Every conversion lives here, none in UI code.
class TimeMap {
public:
    TimeMap() = default;
    TimeMap(qint64 durationMs, qint64 trimInMs, qint64 trimOutMs, const QVector<Fragment> &fragments);

    double outputDurationMs() const { return m_pieces.isEmpty() ? 0 : m_pieces.last().outEnd(); }
    // Empty where the source is cut or outside the trim.
    std::optional<double> toOutput(double srcMs) const;
    // Where `srcMs` lands, or the next kept source after it.
    double toOutputAfter(double srcMs) const;
    // Clamped to the output; monotonic and continuous across seams. In a pause, the frame it
    // holds: the last of the piece before (1 ms short of its end), or the first of the piece
    // after when the pause opens the output.
    double toSource(double outMs) const;
    // P: the pause at `outMs`, if any: the piece after it and how far in.
    struct PauseHit { int piece = -1; double intoMs = 0; double lengthMs = 0; bool black = false; };
    std::optional<PauseHit> pauseAt(double outMs) const;
    const QVector<TimePiece> &pieces() const { return m_pieces; }

private:
    QVector<TimePiece> m_pieces;
};

// Playhead steps for the keyboard (C3), on a map of the whole clip (no trim) so every
// kept frame is reachable. Frames sit on J/L's grid, ms = round(index * 1000 / fps); a
// step into a cut goes on to the first frame after it (forwards) or the last one before
// it (backwards). Results stay in [0, durationMs - 1].
qint64 stepFrames(const TimeMap &clip, qint64 durationMs, double fps, qint64 sourceMs, int frames);
// One stretch of output time (Shift+arrow: a second as it plays), snapped to the grid.
qint64 stepOutput(const TimeMap &clip, qint64 durationMs, double fps, qint64 sourceMs, double outputDeltaMs);
// The nearest edge strictly before (direction < 0) or after (> 0) `sourceMs`, compared in
// output time so edges inside a cut count as its seam; none left: nullopt.
std::optional<qint64> nextEdge(const TimeMap &clip, const QVector<qint64> &edges, qint64 sourceMs, int direction);

}

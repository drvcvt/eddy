#pragma once
#include <QVector>
#include <optional>
#include "studiodocument.h"

namespace eddy {

// One kept stretch of the source and where it lands in the output.
struct TimePiece {
    double srcStart = 0, srcEnd = 0;   // source ms, end exclusive
    double outStart = 0;               // output ms
    double speed = 1;
    double outEnd() const { return outStart + (srcEnd - srcStart) / speed; }
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
    // Clamped to the output; monotonic and continuous across seams.
    double toSource(double outMs) const;
    const QVector<TimePiece> &pieces() const { return m_pieces; }

private:
    QVector<TimePiece> m_pieces;
};

}

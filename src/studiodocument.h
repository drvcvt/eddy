#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>
#include "studiostyle.h"

namespace eddy {

// Time-varying Studio edits (docs/plans/2026-09-24-studio-features.md, 3.2).
// Times are source milliseconds, places are document pixels, so cutting or
// speeding up fragments never moves them. The cursor is not among them:
// Boltsnap bakes it into the video and Eddy keeps it as it is.
struct ZoomSegment {
    quint32 id = 0;                  // stable across edits, for selection and undo
    qint64 startMs = 0, endMs = 0;   // end exclusive
    double scale = 2.0;              // 1.1 .. 4
    enum class Target { Point, Cursor } target = Target::Point;
    QPointF point;                   // centre of the zoomed view
    enum class Motion { Focused, Smooth, Instant } motion = Motion::Focused;
    bool operator==(const ZoomSegment &) const = default;
};

// Fragments partition the source; each runs until the next one starts.
struct Fragment {
    qint64 startMs = 0;
    double speed = 1.0;              // 0.25 .. 4
    bool removed = false;
    bool operator==(const Fragment &) const = default;
};

struct StudioDocument {
    StudioStyle style;
    QVector<ZoomSegment> zooms;      // sorted by start, never overlapping
    QVector<Fragment> fragments;     // empty: the whole source at 1x
    bool keepZoomedIn = false;
    QPointF keepCenter;              // where the narrower view sits without a cursor track

    // Whether an export needs the frame-render path instead of the filter graph.
    bool timeVarying() const;
    bool operator==(const StudioDocument &) const = default;
};

inline constexpr int kStudioFormatVersion = 1;
inline constexpr int kStudioMaxItems = 1000;

QJsonObject studioToJson(const StudioDocument &doc);
// The whole block or nothing: an unusable block is rejected with a reason,
// never repaired. `durationMs` (0 for images) and `source` bound the values.
std::optional<StudioDocument> studioFromJson(const QJsonObject &json, qint64 durationMs,
                                             QSize source, QString *error);

}

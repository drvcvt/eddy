#pragma once
#include <QPointF>
#include <QRectF>
#include <QVector>
#include <cmath>
#include "studiodocument.h"
#include "timemap.h"
#include "cursortrack.h"

namespace eddy {

// Critically damped spring with mass 1, the model Boltsnap uses for its
// cursor: stiffness omega^2, damping 2 omega. 0 means Instant, a jump.
double cameraOmega(ZoomSegment::Motion motion);
// Exact solution over `dt` seconds towards a fixed target: stable for any
// step and identical on every machine, unlike an Euler integrator.
void springStep(double omega, double target, double dt, double &x, double &v);

struct CameraFrame {
    QRectF content;      // document pixels the camera may show: the crop or the whole frame
    double aspect = 0;   // width/height of the output frame it fills; 0 = the content's own
    QPointF baseCenter;  // centre of the unzoomed view when `aspect` narrows it
    // Boltsnap's pointer track: Cursor zooms follow it, and so does a narrowed
    // base view when `baseFollowsCursor` (studio plan 6.2 and 6.5).
    const CursorTrack *cursor = nullptr;
    bool baseFollowsCursor = false;
};

// Moves `centre` only as far as keeps `pointer` inside the central box of
// `deadZone` times `window` (studio plan 6.2).
QPointF followWithDeadZone(QPointF centre, QPointF pointer, QSizeF window, double deadZone = 0.4);

// The camera over output time, simulated once at a fixed rate and cached, so
// preview and export read the same curve (studio plan 3.4). Rebuild it after
// every edit; it never integrates during playback.
class CameraPath {
public:
    static constexpr int kRate = 240;            // samples per output second
    static constexpr double kJoinGapMs = 1000;   // closer zooms hand over directly

    CameraPath() = default;
    CameraPath(const QVector<ZoomSegment> &zooms, const TimeMap &time, const CameraFrame &frame);

    QRectF baseRect() const { return m_base; }
    // Document pixels, always inside the frame's content.
    QRectF rectAt(double outMs) const;
    // Where a zoom's camera comes to rest: its window around the point, kept
    // inside the content. What the editor shows while that zoom is selected.
    QRectF targetRect(const ZoomSegment &zoom) const {
        return rectFor(zoom.point.x(), zoom.point.y(), std::log(zoom.scale));
    }
    // How far the camera is zoomed in at `outMs`; 1 is the base view.
    double zoomAt(double outMs) const {
        const QRectF r = rectAt(outMs);
        return r.width() > 0 ? m_base.width() / r.width() : 1.0;
    }
    int sampleCount() const { return m_samples.size(); }

private:
    struct Sample {
        float x, y, logScale, vx, vy, vLogScale;
        bool jump;   // an Instant cut lands here: hold the previous sample until then
    };
    QRectF rectFor(double x, double y, double logScale) const;

    QVector<Sample> m_samples;
    QRectF m_content, m_base;
};

}

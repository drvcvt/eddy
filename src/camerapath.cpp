#include "camerapath.h"
#include <algorithm>
#include <cmath>

namespace eddy {

double cameraOmega(ZoomSegment::Motion motion) {
    switch (motion) {
    case ZoomSegment::Motion::Focused: return 10.0;   // stiffness 100, damping 20: ~0.58 s to 2 %
    case ZoomSegment::Motion::Smooth: return 6.0;     // stiffness 36, damping 12: ~0.97 s
    case ZoomSegment::Motion::Instant: return 0.0;
    }
    return 10.0;
}

void springStep(double omega, double target, double dt, double &x, double &v) {
    const double x0 = x - target;
    const double b = v + omega * x0;
    const double e = std::exp(-omega * dt);
    x = target + (x0 + b * dt) * e;
    v = (v - omega * b * dt) * e;
}

QPointF followWithDeadZone(QPointF centre, QPointF pointer, QSizeF window, double deadZone) {
    const double hx = window.width() * deadZone / 2, hy = window.height() * deadZone / 2;
    auto follow = [](double c, double p, double half) {
        return p > c + half ? p - half : p < c - half ? p + half : c;
    };
    return QPointF(follow(centre.x(), pointer.x(), hx), follow(centre.y(), pointer.y(), hy));
}

// A centre that keeps a window of half-size `half` inside [lo, hi].
static double clampCentre(double centre, double half, double lo, double hi) {
    return hi - lo <= 2 * half ? (lo + hi) / 2 : std::clamp(centre, lo + half, hi - half);
}

CameraPath::CameraPath(const QVector<ZoomSegment> &zooms, const TimeMap &time, const CameraFrame &frame)
    : m_content(frame.content) {
    QSizeF baseSize = m_content.size();
    if (frame.aspect > 0) {
        const double w = std::min(m_content.width(), m_content.height() * frame.aspect);
        baseSize = QSizeF(w, w / frame.aspect);
    }
    const QPointF wanted = frame.aspect > 0 ? frame.baseCenter : m_content.center();
    m_base = QRectF(QPointF(), baseSize);          // rectFor sizes from m_base
    m_base = rectFor(wanted.x(), wanted.y(), 0);

    // Zooms in output time; cut ones vanish, close ones hand over directly.
    struct Span { double start, end; const ZoomSegment *zoom; };
    QVector<Span> spans;
    for (const ZoomSegment &z : zooms) {
        const double a = time.toOutputAfter(z.startMs), b = time.toOutputAfter(z.endMs);
        if (b > a) spans.append({a, b, &z});
    }
    for (int i = 0; i + 1 < spans.size(); ++i)
        if (spans[i + 1].start - spans[i].end < kJoinGapMs) spans[i].end = spans[i + 1].start;

    const int count = int(std::ceil(time.outputDurationMs() * kRate / 1000.0)) + 1;
    const double dt = 1.0 / kRate;
    QPointF home = m_base.center();
    double x = home.x(), y = home.y(), ls = 0, vx = 0, vy = 0, vls = 0;
    double tx = x, ty = y, tls = 0;
    double omega = cameraOmega(ZoomSegment::Motion::Focused);
    int current = -1, next = 0;
    QPointF followed;   // a Cursor zoom's dead-zoned centre
    // Where the pointer is at output time t; empty without a track or while hidden.
    auto pointer = [&](double t) -> std::optional<QPointF> {
        if (!frame.cursor) return std::nullopt;
        return frame.cursor->positionAt(qint64(std::llround(time.toSource(t))));
    };
    const bool baseFollows = frame.baseFollowsCursor && frame.cursor && frame.aspect > 0;
    // A following base view starts on the pointer instead of gliding to it.
    if (baseFollows) {
        if (const auto first = pointer(0)) {
            const QPointF moved = followWithDeadZone(home, *first, m_base.size());
            home = QPointF(clampCentre(moved.x(), m_base.width() / 2, m_content.left(), m_content.right()),
                           clampCentre(moved.y(), m_base.height() / 2, m_content.top(), m_content.bottom()));
            x = tx = home.x();
            y = ty = home.y();
        }
    }
    m_samples.reserve(count);
    for (int i = 0; i < count; ++i) {
        // The step into sample i follows the target that held before it, so a
        // zoom starts moving at its start, not one step early.
        if (i > 0) {
            springStep(omega, tx, dt, x, vx);
            springStep(omega, ty, dt, y, vy);
            springStep(omega, tls, dt, ls, vls);
        }
        const double t = i * 1000.0 / kRate;
        const std::optional<QPointF> at = pointer(t);
        // A following base view keeps the pointer in its middle 40 %, held
        // where it was while the pointer is away.
        if (baseFollows && at) {
            const QPointF moved = followWithDeadZone(home, *at, m_base.size());
            home = QPointF(clampCentre(moved.x(), m_base.width() / 2, m_content.left(), m_content.right()),
                           clampCentre(moved.y(), m_base.height() / 2, m_content.top(), m_content.bottom()));
        }
        while (next < spans.size() && spans[next].end <= t) ++next;
        const int active = next < spans.size() && spans[next].start <= t ? next : -1;
        const ZoomSegment *zoom = active >= 0 ? spans[active].zoom : nullptr;
        const bool cursorZoom = zoom && zoom->target == ZoomSegment::Target::Cursor && frame.cursor;
        bool jump = false;
        if (active != current) {
            // Moving into a zoom uses its motion, moving out uses the one being left.
            omega = cameraOmega(spans[active >= 0 ? active : current].zoom->motion);
            current = active;
            if (zoom) {
                tls = std::log(zoom->scale);
                followed = cursorZoom && at ? *at : zoom->point;
                tx = clampCentre(followed.x(), m_base.width() / zoom->scale / 2, m_content.left(), m_content.right());
                ty = clampCentre(followed.y(), m_base.height() / zoom->scale / 2, m_content.top(), m_content.bottom());
            } else {
                tls = 0;
            }
            if (omega == 0) {
                if (!zoom) { tx = home.x(); ty = home.y(); }
                x = tx;
                y = ty;
                ls = tls;
                vx = vy = vls = 0;
                jump = true;
            }
        }
        if (cursorZoom && at) {
            const QSizeF window = m_base.size() / zoom->scale;
            followed = followWithDeadZone(followed, *at, window);
            tx = clampCentre(followed.x(), window.width() / 2, m_content.left(), m_content.right());
            ty = clampCentre(followed.y(), window.height() / 2, m_content.top(), m_content.bottom());
        }
        if (!zoom) {
            tx = home.x();
            ty = home.y();
        }
        m_samples.append({float(x), float(y), float(ls), float(vx), float(vy), float(vls),
                          float(home.x()), float(home.y()), jump});
    }
}

QRectF CameraPath::rectFor(double x, double y, double logScale) const {
    const QSizeF size = m_base.size() / std::exp(std::max(0.0, logScale));
    return QRectF(QPointF(clampCentre(x, size.width() / 2, m_content.left(), m_content.right()) - size.width() / 2,
                          clampCentre(y, size.height() / 2, m_content.top(), m_content.bottom()) - size.height() / 2),
                  size);
}

QPointF CameraPath::homeAt(double outMs) const {
    if (m_samples.isEmpty()) return m_base.center();
    const int i = std::clamp(int(std::floor(std::max(0.0, outMs) * kRate / 1000.0 + 1e-9)), 0, int(m_samples.size()) - 1);
    return QPointF(m_samples[i].homeX, m_samples[i].homeY);
}

bool CameraPath::cutWithin(double fromMs, double toMs) const {
    const int first = int(std::floor(std::max(0.0, fromMs) * kRate / 1000.0 + 1e-9)) + 1;
    const int last = std::min(int(std::floor(std::max(0.0, toMs) * kRate / 1000.0 + 1e-9)), int(m_samples.size()) - 1);
    for (int i = first; i <= last; ++i)
        if (m_samples[i].jump) return true;
    return false;
}

QRectF CameraPath::rectAt(double outMs) const {
    if (m_samples.isEmpty()) return m_base;
    // The epsilon keeps a sample time from landing just below its own index.
    const double u = std::max(0.0, outMs) * kRate / 1000.0;
    const int i = int(std::floor(u + 1e-9));
    const Sample &a = m_samples[std::min(i, int(m_samples.size()) - 1)];
    if (i >= m_samples.size() - 1) return rectFor(a.x, a.y, a.logScale);
    const Sample &b = m_samples[i + 1];
    const double f = std::max(0.0, u - i);
    if (b.jump || f == 0) return rectFor(a.x, a.y, a.logScale);
    // Cubic Hermite with the spring's own velocities: smooth between samples.
    const double h = 1.0 / kRate;
    const double f2 = f * f, f3 = f2 * f;
    const double h00 = 2 * f3 - 3 * f2 + 1, h10 = f3 - 2 * f2 + f, h01 = -2 * f3 + 3 * f2, h11 = f3 - f2;
    auto blend = [&](double p0, double v0, double p1, double v1) {
        return h00 * p0 + h10 * h * v0 + h01 * p1 + h11 * h * v1;
    };
    return rectFor(blend(a.x, a.vx, b.x, b.vx), blend(a.y, a.vy, b.y, b.vy),
                   blend(a.logScale, a.vLogScale, b.logScale, b.vLogScale));
}

}

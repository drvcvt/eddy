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
    const QPointF home = m_base.center();
    double x = home.x(), y = home.y(), ls = 0, vx = 0, vy = 0, vls = 0;
    double tx = x, ty = y, tls = 0;
    double omega = cameraOmega(ZoomSegment::Motion::Focused);
    int current = -1, next = 0;
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
        while (next < spans.size() && spans[next].end <= t) ++next;
        const int active = next < spans.size() && spans[next].start <= t ? next : -1;
        bool jump = false;
        if (active != current) {
            // Moving into a zoom uses its motion, moving out uses the one being left.
            omega = cameraOmega(spans[active >= 0 ? active : current].zoom->motion);
            current = active;
            if (active >= 0) {
                const ZoomSegment &z = *spans[active].zoom;
                tls = std::log(z.scale);
                tx = clampCentre(z.point.x(), m_base.width() / z.scale / 2, m_content.left(), m_content.right());
                ty = clampCentre(z.point.y(), m_base.height() / z.scale / 2, m_content.top(), m_content.bottom());
            } else {
                tx = home.x();
                ty = home.y();
                tls = 0;
            }
            if (omega == 0) {
                x = tx;
                y = ty;
                ls = tls;
                vx = vy = vls = 0;
                jump = true;
            }
        }
        m_samples.append({float(x), float(y), float(ls), float(vx), float(vy), float(vls), jump});
    }
}

QRectF CameraPath::rectFor(double x, double y, double logScale) const {
    const QSizeF size = m_base.size() / std::exp(std::max(0.0, logScale));
    return QRectF(QPointF(clampCentre(x, size.width() / 2, m_content.left(), m_content.right()) - size.width() / 2,
                          clampCentre(y, size.height() / 2, m_content.top(), m_content.bottom()) - size.height() / 2),
                  size);
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

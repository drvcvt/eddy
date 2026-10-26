#include "app/spring.h"
#include <cmath>

namespace eddy {

static bool g_enabled = true;

void Spring::setEnabled(bool on) { g_enabled = on; }
bool Spring::enabled() { return g_enabled; }

// With no bounce the natural frequency follows from the duration alone:
// omega = 2 pi / d settles within about d, like SwiftUI's spring(duration:).
Spring::Spring(int durationMs, std::function<void(qreal)> apply, QObject *parent)
    : QAbstractAnimation(parent), m_omega(2 * M_PI / (durationMs / 1000.0)), m_apply(std::move(apply)) {}

void Spring::sample(qreal t, qreal *value, qreal *velocity) const {
    // x(t) = target + (c1 + c2 t) e^(-omega t), the critically damped solution.
    const qreal c1 = m_from - m_target;
    const qreal c2 = m_velocity + m_omega * c1;
    const qreal decay = std::exp(-m_omega * t);
    *value = m_target + (c1 + c2 * t) * decay;
    *velocity = (c2 - m_omega * (c1 + c2 * t)) * decay;
}

qreal Spring::value() const {
    if (state() != Running) return m_from;
    qreal x, v;
    sample(m_clock.nsecsElapsed() / 1e9, &x, &v);
    return x;
}

void Spring::setTarget(qreal target) {
    if (!g_enabled) { jumpTo(target); return; }
    if (state() == Running) {
        sample(m_clock.nsecsElapsed() / 1e9, &m_from, &m_velocity);
    } else {
        if (qFuzzyCompare(1 + m_from, 1 + target)) { m_target = target; return; }
        m_velocity = 0;
    }
    m_target = target;
    m_clock.start();
    if (state() != Running) start();
}

void Spring::jumpTo(qreal value) {
    stop();
    m_from = m_target = value;
    m_velocity = 0;
    if (m_apply) m_apply(value);
}

void Spring::updateCurrentTime(int) {
    qreal x, v;
    sample(m_clock.nsecsElapsed() / 1e9, &x, &v);
    // At rest when the remaining step and speed are below what shows: land exactly.
    const qreal span = std::max<qreal>(1e-3, std::abs(m_target - m_from) + std::abs(m_velocity) / m_omega);
    if (std::abs(x - m_target) < 1e-3 * span && std::abs(v) < 1e-2 * span * m_omega) {
        jumpTo(m_target);
        return;
    }
    if (m_apply) m_apply(x);
}

}

#pragma once
#include <QAbstractAnimation>
#include <QElapsedTimer>
#include <QHash>
#include <functional>

namespace eddy {

// The motion tokens (docs/design.md): a perceptual duration each, no bounce.
namespace motion {
inline constexpr int kFast = 120;   // hover, press, focus, a selection fill
inline constexpr int kBase = 220;   // a popover, floating bar or toast in and out
inline constexpr int kSlow = 320;   // the timeline growing, a page switch
}

// A critically damped spring, evaluated from the time since its last target,
// never stepped per frame. A new target mid-flight keeps the value on screen
// and its velocity, so motion turns instead of restarting.
class Spring : public QAbstractAnimation {
    Q_OBJECT
public:
    Spring(int durationMs, std::function<void(qreal)> apply, QObject *parent = nullptr);
    void setTarget(qreal target);
    void jumpTo(qreal value);   // no motion: a reset, or reduced motion
    qreal value() const;
    qreal target() const { return m_target; }
    int duration() const override { return -1; }
    // Off (--no-anim, `animations=false`): every spring lands on its target at once.
    static void setEnabled(bool on);
    static bool enabled();

protected:
    void updateCurrentTime(int) override;

private:
    void sample(qreal seconds, qreal *value, qreal *velocity) const;
    qreal m_omega;
    qreal m_from = 0, m_velocity = 0, m_target = 0;
    QElapsedTimer m_clock;   // since the last target
    std::function<void(qreal)> m_apply;
};

// One spring per key for the many small states of one widget (a timeline's grips,
// zooms, notches): aim a key on or off, read its level while painting. A key seen for
// the first time takes its state at once; after that every change eases.
class SpringLevels : public QObject {
public:
    SpringLevels(int durationMs, std::function<void()> changed, QObject *parent = nullptr)
        : QObject(parent), m_duration(durationMs), m_changed(std::move(changed)) {}
    void aim(quint64 key, bool on);
    qreal value(quint64 key) const { return m_values.value(key); }

private:
    int m_duration;
    std::function<void()> m_changed;
    QHash<quint64, Spring *> m_springs;
    QHash<quint64, qreal> m_values;
};

}

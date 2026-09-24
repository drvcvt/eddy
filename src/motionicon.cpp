#include "motionicon.h"
#include "camerapath.h"
#include <QCoreApplication>
#include <QIconEngine>
#include <QPainter>

namespace eddy {

// The path runs 1.4 units (half the stroke) inside the 20-unit live area.
static constexpr double kLeft = 3.4, kRight = 20.6, kLead = 0.25, kShown = 1.45;

QPolygonF motionCurve(ZoomSegment::Motion motion) {
    const double omega = cameraOmega(motion);
    auto px = [](double t) { return kLeft + t / kShown * (kRight - kLeft); };
    auto py = [](double value) { return kRight - value * (kRight - kLeft); };
    if (omega == 0)   // Instant: straight up at the step
        return QPolygonF{{px(0), py(0)}, {px(kLead), py(0)}, {px(kLead), py(1)}, {px(kShown), py(1)}};
    QPolygonF curve{{px(0), py(0)}, {px(kLead), py(0)}};
    constexpr int kSteps = 72;
    const double dt = (kShown - kLead) / kSteps;
    double x = 0, v = 0;
    for (int i = 1; i <= kSteps; ++i) {
        springStep(omega, 1, dt, x, v);
        curve << QPointF(px(kLead + i * dt), py(x));
    }
    return curve;
}

namespace {
// Drawn at the real device size on every paint, like theme::tintedIcon.
class MotionEngine : public QIconEngine {
public:
    MotionEngine(ZoomSegment::Motion motion, QColor ink) : m_motion(motion), m_ink(ink) {}
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(rect.width() / 24.0, rect.height() / 24.0);
        painter->setPen(QPen(m_ink, 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(motionCurve(m_motion));
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return pixmap;
    }
    QIconEngine *clone() const override { return new MotionEngine(m_motion, m_ink); }
private:
    ZoomSegment::Motion m_motion;
    QColor m_ink;
};
}

QIcon motionIcon(ZoomSegment::Motion motion, const QColor &ink, int) {
    return QIcon(new MotionEngine(motion, ink));
}

QString motionName(ZoomSegment::Motion motion) {
    switch (motion) {
    case ZoomSegment::Motion::Focused: return QCoreApplication::translate("eddy", "Focused");
    case ZoomSegment::Motion::Smooth: return QCoreApplication::translate("eddy", "Smooth");
    case ZoomSegment::Motion::Instant: return QCoreApplication::translate("eddy", "Instant");
    }
    return {};
}

}

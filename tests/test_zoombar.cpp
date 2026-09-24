#include <QtTest>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <cmath>
#include "motionicon.h"
#include "zoombar.h"

using namespace eddy;

// The height the curve reaches at `seconds` after the step, 0..1.
static double curveAt(const QPolygonF &curve, double seconds) {
    const double x = 3.4 + (0.25 + seconds) / 1.45 * 17.2;
    QPointF best = curve.first();
    for (const QPointF &p : curve)
        if (qAbs(p.x() - x) < qAbs(best.x() - x)) best = p;
    return (20.6 - best.y()) / 17.2;
}

class TestZoomBar : public QObject {
    Q_OBJECT
private slots:
    void motionCurvesAreTheCamerasSpring() {
        // Critically damped step response: 1 - (1 + w t) e^(-w t).
        auto step = [](double w, double t) { return 1 - (1 + w * t) * std::exp(-w * t); };
        const QPolygonF focused = motionCurve(ZoomSegment::Motion::Focused);
        const QPolygonF smooth = motionCurve(ZoomSegment::Motion::Smooth);
        for (double t : {0.1, 0.3, 0.6}) {
            QVERIFY(qAbs(curveAt(focused, t) - step(10, t)) < 0.04);
            QVERIFY(qAbs(curveAt(smooth, t) - step(6, t)) < 0.04);
        }
        QCOMPARE(curveAt(focused, -0.1), 0.0);
        const QPolygonF instant = motionCurve(ZoomSegment::Motion::Instant);
        QCOMPARE(curveAt(instant, 0.8), 1.0);
    }
    void motionIconsShareTheIconGrid() {
        for (auto motion : {ZoomSegment::Motion::Focused, ZoomSegment::Motion::Smooth,
                            ZoomSegment::Motion::Instant}) {
            const QImage sheet = motionIcon(motion, Qt::black, 240).pixmap(240, 240).toImage();
            int left = 240, top = 240, right = -1, bottom = -1;
            for (int y = 0; y < sheet.height(); ++y)
                for (int x = 0; x < sheet.width(); ++x)
                    if (qAlpha(sheet.pixel(x, y))) {
                        left = qMin(left, x); right = qMax(right, x);
                        top = qMin(top, y); bottom = qMax(bottom, y);
                    }
            const double unit = sheet.width() / 24.0;
            const double width = (right - left + 1) / unit, height = (bottom - top + 1) / unit;
            QVERIFY2(qAbs((left + right + 1) / 2.0 / unit - 12) <= 0.2, qPrintable(motionName(motion)));
            QVERIFY2(qAbs((top + bottom + 1) / 2.0 / unit - 12) <= 0.3, qPrintable(motionName(motion)));
            QVERIFY2(qAbs(qMax(width, height) - 20) <= 0.3,
                     qPrintable(QStringLiteral("%1 live %2").arg(motionName(motion)).arg(qMax(width, height))));
        }
    }
    void barShowsTheZoomAndReportsChoices() {
        ZoomBar bar;
        ZoomSegment z;
        z.scale = 2;
        z.motion = ZoomSegment::Motion::Smooth;
        bar.setZoom(z);
        auto *scale = bar.findChild<QToolButton *>(QStringLiteral("ZoomScale"));
        auto *motion = bar.findChild<QToolButton *>(QStringLiteral("ZoomMotion"));
        QVERIFY(scale && motion);
        QCOMPARE(scale->text(), QStringLiteral("2×"));
        QCOMPARE(motion->text(), QStringLiteral("Smooth"));
        z.scale = 2.4;
        bar.setZoom(z);
        QCOMPARE(scale->text(), QStringLiteral("2.4×"));
        QSignalSpy scales(&bar, &ZoomBar::scaleChosen);
        QSignalSpy motions(&bar, &ZoomBar::motionChosen);
        QSignalSpy removes(&bar, &ZoomBar::removeRequested);
        bar.findChild<QMenu *>(QStringLiteral("ZoomScaleMenu"))->actions().at(3)->trigger();
        QCOMPARE(scales.first().first().toDouble(), 3.0);
        bar.findChild<QMenu *>(QStringLiteral("ZoomMotionMenu"))->actions().at(2)->trigger();
        QCOMPARE(motions.first().first().value<ZoomSegment::Motion>(), ZoomSegment::Motion::Instant);
        bar.findChild<QToolButton *>(QStringLiteral("ZoomRemove"))->click();
        QCOMPARE(removes.count(), 1);
    }
};

QTEST_MAIN(TestZoomBar)
#include "test_zoombar.moc"

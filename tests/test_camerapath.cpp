#include <QtTest>
#include <cmath>
#include "camerapath.h"

using namespace eddy;

static const QRectF kContent(0, 0, 1920, 1080);

static ZoomSegment zoom(qint64 start, qint64 end, double scale, QPointF point,
                        ZoomSegment::Motion motion = ZoomSegment::Motion::Focused) {
    ZoomSegment z;
    z.id = quint32(start + 1);
    z.startMs = start;
    z.endMs = end;
    z.scale = scale;
    z.point = point;
    z.motion = motion;
    return z;
}

static CameraPath path(const QVector<ZoomSegment> &zooms, qint64 duration = 10000,
                       const QVector<Fragment> &fragments = {}) {
    return CameraPath(zooms, TimeMap(duration, 0, duration, fragments), CameraFrame{kContent, 0, {}});
}

// How far the camera is zoomed in, on the log scale the springs work in.
static double logZoom(const CameraPath &p, double outMs) {
    return std::log(p.baseRect().width() / p.rectAt(outMs).width());
}

static bool near(const QRectF &a, const QRectF &b, double tolerance = 1e-3) {
    return qAbs(a.left() - b.left()) <= tolerance && qAbs(a.top() - b.top()) <= tolerance
        && qAbs(a.width() - b.width()) <= tolerance && qAbs(a.height() - b.height()) <= tolerance;
}

class TestCameraPath : public QObject {
    Q_OBJECT
private slots:
    void springStepIsExact() {
        // Two half steps land where one full step does, and both match the closed form.
        double x1 = 3, v1 = -2, x2 = 3, v2 = -2;
        springStep(7, 1, 0.1, x1, v1);
        springStep(7, 1, 0.05, x2, v2);
        springStep(7, 1, 0.05, x2, v2);
        QVERIFY(qAbs(x1 - x2) < 1e-12 && qAbs(v1 - v2) < 1e-12);
        const double closed = 1 + (2 + (-2 + 7 * 2) * 0.1) * std::exp(-0.7);
        QVERIFY(qAbs(x1 - closed) < 1e-12);
    }

    void withoutZoomsTheCameraShowsEverything() {
        const CameraPath p = path({});
        for (double t : {0.0, 1234.5, 10000.0, 20000.0})
            QVERIFY(near(p.rectAt(t), kContent, 0));
    }

    void focusedSettlesInAboutSixTenthsOfASecond() {
        const CameraPath p = path({zoom(1000, 5000, 2, kContent.center())});
        const double target = std::log(2.0);
        QVERIFY(near(p.rectAt(999), kContent, 0));   // nothing moves before the zoom
        QVERIFY(near(p.rectAt(1000), kContent, 0));
        QVERIFY(qAbs(logZoom(p, 1500) - target) > 0.02 * target);
        QVERIFY(qAbs(logZoom(p, 1620) - target) < 0.02 * target);
    }

    void smoothTakesAboutASecond() {
        const CameraPath p = path({zoom(1000, 5000, 2, kContent.center(), ZoomSegment::Motion::Smooth)});
        const double target = std::log(2.0);
        QVERIFY(qAbs(logZoom(p, 1900) - target) > 0.02 * target);
        QVERIFY(qAbs(logZoom(p, 2000) - target) < 0.02 * target);
    }

    void instantJumpsExactlyBothWays() {
        const QPointF point(1400, 300);
        const CameraPath p = path({zoom(1000, 3000, 2, point, ZoomSegment::Motion::Instant)});
        QVERIFY(near(p.rectAt(999.9), kContent, 0));
        const QRectF zoomed = p.rectAt(1000);
        QVERIFY(near(zoomed, QRectF(point.x() - 480, point.y() - 270, 960, 540)));
        QVERIFY(near(p.rectAt(2999), zoomed));
        QVERIFY(near(p.rectAt(3000), kContent));
    }

    void neverShowsAnythingOutsideTheContent() {
        const CameraPath p = path({zoom(500, 2500, 4, QPointF(0, 0)),
                                   zoom(2600, 4000, 3, QPointF(1920, 1080), ZoomSegment::Motion::Smooth),
                                   zoom(6000, 7000, 1.1, QPointF(1919, 5))});
        const QRectF allowed = kContent.adjusted(-1e-3, -1e-3, 1e-3, 1e-3);
        for (double t = 0; t <= 10000; t += 1)
            QVERIFY2(allowed.contains(p.rectAt(t)), qPrintable(QString::number(t)));
    }

    void returnsToTheFullViewAfterTheLastZoom() {
        const CameraPath p = path({zoom(1000, 2000, 2, QPointF(300, 300))});
        QVERIFY(near(p.rectAt(4000), kContent, 0.01));
    }

    void sameInputGivesTheSameCurve() {
        const QVector<ZoomSegment> zooms{zoom(700, 3100, 2.5, QPointF(400, 900)),
                                         zoom(3300, 5000, 1.5, QPointF(1500, 200), ZoomSegment::Motion::Smooth)};
        const CameraPath a = path(zooms), b = path(zooms);
        for (int i = 0; i < 997; ++i) {
            const double t = std::fmod(i * 7919.0, 10000.0) + i * 0.001;
            const QRectF ra = a.rectAt(t), rb = b.rectAt(t);
            QVERIFY(ra.x() == rb.x() && ra.y() == rb.y() && ra.width() == rb.width() && ra.height() == rb.height());
        }
    }

    void closeZoomsHandOverWithoutZoomingOut() {
        const QPointF c = kContent.center();
        const CameraPath joined = path({zoom(1000, 2000, 2, c), zoom(2600, 3600, 2, c)});
        QVERIFY(logZoom(joined, 2300) > 0.95 * std::log(2.0));
        const CameraPath apart = path({zoom(1000, 2000, 2, c), zoom(5000, 6000, 2, c)});
        QVERIFY(logZoom(apart, 3500) < 0.05 * std::log(2.0));
    }

    void zoomsFollowFragments() {
        Fragment fast;
        fast.speed = 2;
        const CameraPath sped = path({zoom(2000, 6000, 2, QPointF(500, 500), ZoomSegment::Motion::Instant)},
                                     10000, {fast});
        QVERIFY(near(sped.rectAt(999), kContent, 0));
        QVERIFY(!near(sped.rectAt(1000), kContent));
        QVERIFY(!near(sped.rectAt(2999), kContent));
        QVERIFY(near(sped.rectAt(3000), kContent));
        Fragment kept, cut, after;
        cut.startMs = 2000;
        cut.removed = true;
        after.startMs = 5000;
        const CameraPath inCut = path({zoom(2500, 4500, 2, QPointF(500, 500))}, 10000, {kept, cut, after});
        for (double t = 0; t <= 7000; t += 25)
            QVERIFY(near(inCut.rectAt(t), kContent, 0));
    }

    void narrowOutputsShowTheLargestFittingWindow() {
        const TimeMap time(10000, 0, 10000, {});
        const CameraPath left({}, time, CameraFrame{QRectF(0, 0, 1600, 900), 9.0 / 16, QPointF(100, 450)});
        QVERIFY(near(left.baseRect(), QRectF(0, 0, 506.25, 900), 1e-9));
        const CameraPath middle({}, time, CameraFrame{QRectF(0, 0, 1600, 900), 9.0 / 16, QPointF(800, 450)});
        QVERIFY(near(middle.baseRect(), QRectF(546.875, 0, 506.25, 900), 1e-9));
        QVERIFY(near(middle.rectAt(5000), middle.baseRect()));
    }
};

QTEST_GUILESS_MAIN(TestCameraPath)
#include "test_camerapath.moc"

#include <QtTest>
#include <QSlider>
#include <QToolButton>
#include "studiopopover.h"

using namespace eddy;

static QToolButton *button(QWidget &w, const QString &name, const QString &text) {
    for (auto *b : w.findChildren<QToolButton *>(name))
        if (b->text() == text) return b;
    return nullptr;
}

class TestStudioPopover : public QObject {
    Q_OBJECT
private slots:
    void imagesHaveNoCameraPage() {
        StudioPopover popover(StudioStyle(), QSize(200, 100), {}, nullptr);
        QVERIFY(popover.findChildren<QToolButton *>(QStringLiteral("StudioPage")).isEmpty());
    }
    void cameraPageSetsMotionAndKeepZoomedIn() {
        StudioStyle style;
        style.background = StudioStyle::Background::Color;
        StudioCameraSettings camera;
        camera.available = true;
        camera.motion = ZoomSegment::Motion::Focused;
        camera.motionBlur = 30;
        StudioPopover popover(style, QSize(1920, 1080), camera, nullptr);
        popover.show();
        const QSize size = popover.size();
        const int right = popover.geometry().right();
        button(popover, QStringLiteral("StudioPage"), QStringLiteral("Camera"))->click();
        // The Camera page is shorter; the popover shrinks and keeps its right edge.
        QVERIFY(popover.height() < size.height());
        QCOMPARE(popover.geometry().right(), right);
        button(popover, QStringLiteral("StudioPage"), QStringLiteral("Style"))->click();
        QCOMPARE(popover.size(), size);
        QSignalSpy motions(&popover, &StudioPopover::motionChosen);
        QVERIFY(button(popover, QStringLiteral("StudioMotion"), QStringLiteral("Focused"))->isChecked());
        button(popover, QStringLiteral("StudioMotion"), QStringLiteral("Smooth"))->click();
        QCOMPARE(motions.first().first().value<ZoomSegment::Motion>(), ZoomSegment::Motion::Smooth);
        auto *keep = popover.findChild<QToolButton *>(QStringLiteral("StudioKeepZoomed"));
        QVERIFY(keep && !keep->isEnabled());
        popover.setKeepZoomedInAvailable(true);
        QVERIFY(keep->isEnabled());
        QSignalSpy keeps(&popover, &StudioPopover::keepZoomedInChanged);
        keep->click();
        QCOMPARE(keeps.first().first().toBool(), true);
        auto *blur = popover.findChild<QSlider *>(QStringLiteral("StudioMotionBlur"));
        QVERIFY(blur);
        QCOMPARE(blur->value(), 30);
        QSignalSpy blurs(&popover, &StudioPopover::motionBlurChanged);
        blur->setValue(70);
        QCOMPARE(blurs.first().first().toInt(), 70);
    }
    void mixedMotionsCheckNone() {
        StudioCameraSettings camera;
        camera.available = true;
        StudioPopover popover(StudioStyle(), QSize(1920, 1080), camera, nullptr);
        for (auto *b : popover.findChildren<QToolButton *>(QStringLiteral("StudioMotion")))
            QVERIFY(!b->isChecked());
    }
};

QTEST_MAIN(TestStudioPopover)
#include "test_studiopopover.moc"

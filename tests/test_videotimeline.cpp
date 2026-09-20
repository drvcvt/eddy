#include <QtTest>
#include "videotimeline.h"

using namespace eddy;

class TestVideoTimeline : public QObject {
    Q_OBJECT
private slots:
    void releaseUsesFinalPointerAndCancelRestoresRange() {
        VideoTimeline timeline;
        timeline.resize(300, 38);
        timeline.setDuration(1000);
        timeline.show();
        QSignalSpy commits(&timeline, &VideoTimeline::trimCommitted);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(6, 22));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(78, 22));
        QCOMPARE(timeline.trimIn(), 250);
        QCOMPARE(commits.count(), 1);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(78, 22));
        QTest::mouseMove(&timeline, QPoint(150, 22));
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(150, 22));
        QCOMPARE(timeline.trimIn(), 250);
        QCOMPARE(commits.count(), 1);
    }
    void fineTrimPreservesGrabOffsetAndRejectsPlayerFeedback() {
        VideoTimeline timeline;
        timeline.resize(300, 38);
        timeline.setDuration(10000);
        timeline.setTrimRange(2500, 10000);
        timeline.show();
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(82, 22));
        QCOMPARE(timeline.trimIn(), 2500);
        QMouseEvent move(QEvent::MouseMove, QPointF(226, 22), QPointF(226, 22),
                         Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        QApplication::sendEvent(&timeline, &move);
        QCOMPARE(timeline.trimIn(), 3000);
        const auto target = timeline.position();
        timeline.setPosition(10);
        QCOMPARE(timeline.position(), target);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(226, 22));
        QCOMPARE(timeline.trimIn(), 3000);
    }
    void zoomKeepsAnchorAndDoesNotEditTrim() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        QSignalSpy commits(&timeline, &VideoTimeline::trimCommitted);
        timeline.zoomAt(2, 2500);
        QCOMPARE(timeline.visibleStart(), 1250);
        QCOMPARE(timeline.visibleEnd(), 6250);
        timeline.panBy(10000);
        QCOMPARE(timeline.visibleEnd(), 10000);
        QCOMPARE(timeline.trimIn(), 0);
        QCOMPARE(timeline.trimOut(), 10000);
        QCOMPARE(commits.count(), 0);
        timeline.fitClip();
        QCOMPARE(timeline.visibleStart(), 0);
        QCOMPARE(timeline.visibleEnd(), 10000);
    }
    void seekAndTrimRemainSeparateAndCommitOnce() {
        VideoTimeline timeline;
        timeline.resize(300, 38);
        timeline.setDuration(1000);
        timeline.setMinimumRange(40);
        timeline.show();
        QSignalSpy seeks(&timeline, &VideoTimeline::seekRequested);
        QSignalSpy commits(&timeline, &VideoTimeline::trimCommitted);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(150, 19));
        QCOMPARE(timeline.position(), 500);
        QCOMPARE(timeline.trimIn(), 0);
        QCOMPARE(timeline.trimOut(), 1000);
        QCOMPARE(commits.count(), 0);
        QVERIFY(!seeks.isEmpty());

        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(6, 19));
        QTest::mouseMove(&timeline, QPoint(78, 19));
        QCOMPARE(commits.count(), 0);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(78, 19));
        QCOMPARE(timeline.trimIn(), 250);
        QCOMPARE(commits.count(), 1);
        QCOMPARE(commits.at(0).at(0).toLongLong(), 250);

        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(294, 19));
        QTest::mouseMove(&timeline, QPoint(78, 19));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(78, 19));
        QCOMPARE(timeline.trimOut(), 290);
        QCOMPARE(commits.count(), 2);
    }
    void keepsTrimInsideDurationAndOneFrameApart() {
        VideoTimeline timeline;
        timeline.setDuration(1000);
        timeline.setMinimumRange(40);

        timeline.setTrimRange(900, 100);
        QCOMPARE(timeline.trimIn(), 900);
        QCOMPARE(timeline.trimOut(), 940);

        timeline.setTrimRange(-50, 2000);
        QCOMPARE(timeline.trimIn(), 0);
        QCOMPARE(timeline.trimOut(), 1000);
    }

    void clampsPlayheadToCompleteSource() {
        VideoTimeline timeline;
        timeline.setDuration(1000);
        timeline.setPosition(1200);
        QCOMPARE(timeline.position(), 1000);
        timeline.setPosition(-1);
        QCOMPARE(timeline.position(), 0);
    }

    void storesContactSheetWithoutDiskState() {
        VideoTimeline timeline;
        QImage sheet(320, 45, QImage::Format_RGB32);
        sheet.fill(Qt::red);
        timeline.setContactSheet(sheet, 4);
        QVERIFY(timeline.hasContactSheet());
        QCOMPARE(timeline.contactSheetFrameCount(), 4);
    }
};

QTEST_MAIN(TestVideoTimeline)
#include "test_videotimeline.moc"

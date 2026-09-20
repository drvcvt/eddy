#include <QtTest>
#include "videotimeline.h"

using namespace eddy;

class TestVideoTimeline : public QObject {
    Q_OBJECT
private slots:
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

#include <QtTest>
#include "videotimeline.h"

using namespace eddy;

class TestVideoTimeline : public QObject {
    Q_OBJECT
private slots:
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

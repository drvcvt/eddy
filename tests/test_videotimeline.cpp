#include <QtTest>
#include <QSignalSpy>
#include "videotimeline.h"

using namespace eddy;

class TestVideoTimeline : public QObject {
    Q_OBJECT
private slots:
    void offscreenHandlesCannotBeGrabbedAndZeroDurationIsSafe() {
        VideoTimeline timeline;
        timeline.resize(300, 52); timeline.setDuration(0); timeline.fitClip();
        QCOMPARE(timeline.visibleStart(), 0); QCOMPARE(timeline.visibleEnd(), 0);
        timeline.setDuration(10000); timeline.zoomAt(10000.0 / 9000, 100); timeline.show();
        QSignalSpy commits(&timeline, &VideoTimeline::trimCommitted);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(6, 22));
        QVERIFY(!timeline.trimming());
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(70, 22));
        QCOMPARE(timeline.trimIn(), 0); QCOMPARE(commits.count(), 0);
    }
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
    void zoomLaneGrowsTheTimelineUnderTheFilmStrip() {
        VideoTimeline timeline;
        QCOMPARE(timeline.height(), 52);
        QVERIFY(timeline.zoomLaneRect().isEmpty());
        timeline.setZoomLaneVisible(true);
        QCOMPARE(timeline.height(), 84);
        QCOMPARE(timeline.zoomLaneRect().top(), 52.0);
        QCOMPARE(timeline.zoomLaneRect().height(), 28.0);
        timeline.setZoomLaneVisible(false);
        QCOMPARE(timeline.height(), 52);
    }
    void clickingTheEmptyLaneAsksForAZoomThere() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        timeline.show();
        QSignalSpy add(&timeline, &VideoTimeline::zoomAddRequested);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(add.count(), 1);
        QCOMPARE(add.first().first().toLongLong(), 5000);
    }
    void draggingAZoomMovesItAsOneEdit() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 1; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        timeline.show();
        QSignalSpy selected(&timeline, &VideoTimeline::zoomSelected);
        QSignalSpy edited(&timeline, &VideoTimeline::zoomsEdited);
        // A plain click selects without moving, even next to an anchor.
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(96, 66));
        QCOMPARE(selected.count(), 1);
        QCOMPARE(timeline.selectedZoom(), 1u);
        QCOMPARE(edited.count(), 0);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(96, 66));
        QTest::mouseMove(&timeline, QPoint(126, 66));
        QTest::mouseMove(&timeline, QPoint(156, 66));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(edited.count(), 1);
        const auto after = edited.first().at(1).value<QVector<ZoomSegment>>();
        QCOMPARE(after.first().startMs, 4000);
        QCOMPARE(after.first().endMs, 6000);
        QCOMPARE(timeline.zooms().first().startMs, 4000);
    }
    void holdingAZoomWithoutMovingNeverMovesIt() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 1; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        timeline.show();
        QSignalSpy edited(&timeline, &VideoTimeline::zoomsEdited);
        // The edge-pan timer must not drag with a stale pointer while the button is held.
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(96, 66));
        QTest::qWait(120);
        QCOMPARE(timeline.zooms().first().startMs, 2000);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(96, 66));
        QCOMPARE(edited.count(), 0);
    }
    void edgesSnapToThePlayheadAndEscapeRestores() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setPosition(7000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 3; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        timeline.show();
        QSignalSpy edited(&timeline, &VideoTimeline::zoomsEdited);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(126, 66));   // end edge
        QTest::mouseMove(&timeline, QPoint(170, 66));
        QTest::mouseMove(&timeline, QPoint(218, 66));   // 7066 ms, 6 px snap reach = 200 ms
        QCOMPARE(timeline.zooms().first().endMs, 7000);
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QCOMPARE(timeline.zooms().first().endMs, 4000);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(218, 66));
        QCOMPARE(edited.count(), 0);
    }
    void rightClickOnAZoomAsksForItsMenu() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 5; z.startMs = 2000; z.endMs = 4000;
        timeline.setZooms({z});
        QSignalSpy menu(&timeline, &VideoTimeline::zoomMenuRequested);
        emit timeline.customContextMenuRequested(QPoint(96, 66));
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.first().first().toUInt(), 5u);
        QCOMPARE(timeline.selectedZoom(), 5u);
    }
    void laneShowsTheCameraCurve() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        ZoomSegment z;
        z.id = 1; z.startMs = 2000; z.endMs = 8000; z.scale = 2;
        timeline.setZooms({z}, [](qint64 t) { return t >= 3000 && t < 8000 ? 2.0 : 1.0; });
        const QImage shot = timeline.grab().toImage();
        // Zoomed in, the curve fills the block near its top; before the ramp it does not.
        QVERIFY(qGray(shot.pixel(156, 58)) != qGray(shot.pixel(72, 58)));
    }
    void fragmentsShapeTheTimeAxis() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        // Keep 0-4 s, cut 4-6 s, 6-10 s at 2x: 4 + 2 = 6 s wide.
        timeline.setFragments({{0, 1.0, false}, {4000, 1.0, true}, {6000, 2.0, false}});
        timeline.show();
        QCOMPARE(timeline.visibleStart(), 0);
        QCOMPARE(timeline.visibleEnd(), 10000);
        QSignalSpy seeks(&timeline, &VideoTimeline::seekRequested);
        // Five sixths across is edited 5 s, which is source 8 s in the 2x fragment.
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(256, 36));
        QVERIFY(!seeks.isEmpty());
        QCOMPARE(seeks.last().first().toLongLong(), 8000);
        // The cut's notch sits over the seam at edited 4 s.
        QSignalSpy cuts(&timeline, &VideoTimeline::cutClicked);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(206, 14));
        QCOMPARE(cuts.count(), 1);
        QCOMPARE(cuts.first().first().toInt(), 1);
        // Without fragments the axis is the source again.
        timeline.setFragments({});
        seeks.clear();
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 36));
        QCOMPARE(seeks.last().first().toLongLong(), 5000);
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

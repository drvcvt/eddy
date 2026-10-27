#include <QtTest>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QScopeGuard>
#include "timeline/videotimeline.h"
#include "app/spring.h"
#include "timeline/audiowaveform.h"
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace eddy;

class TestVideoTimeline : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { Spring::setEnabled(false); }   // heights land at once unless a test says otherwise
    void lanesOpenOnTheSlowSpring() {
        Spring::setEnabled(true);
        const auto off = qScopeGuard([] { Spring::setEnabled(false); });
        VideoTimeline timeline;
        timeline.resize(300, VideoTimeline::kBaseH);
        timeline.show();
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        timeline.setZoomLaneVisible(true);
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH);   // no jump on the first frame
        QTest::qWait(80);
        QVERIFY2(timeline.height() > VideoTimeline::kBaseH
                 && timeline.height() < VideoTimeline::kBaseH + VideoTimeline::kLaneStep,
                 qPrintable(QString::number(timeline.height())));
        QTRY_COMPARE_WITH_TIMEOUT(timeline.height(), VideoTimeline::kBaseH + VideoTimeline::kLaneStep, 1000);
        // Closed mid-way, it turns back from where it is.
        timeline.setZoomLaneVisible(false);
        QTest::qWait(40);
        timeline.setZoomLaneVisible(true);
        QVERIFY(timeline.height() > VideoTimeline::kBaseH);
        QTRY_COMPARE_WITH_TIMEOUT(timeline.height(), VideoTimeline::kBaseH + VideoTimeline::kLaneStep, 1000);
    }
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
    void gripsAreFoundInEditedTime() {
        // A sped-up first piece: source 8 s is edited 2 s, inside a view of edited 0 to 3 s.
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.setFragments({{0, 4.0, false}, {8000, 1.0, false}});
        timeline.setTrimRange(0, 8000);
        timeline.show();
        timeline.zoomAt(4000.0 / 3000.0, 0);   // edited 4 s in all: view edited 0 to 3 s
        QCOMPARE(timeline.visibleStart(), 0);
        QVERIFY(timeline.visibleEnd() > 8000);   // source 8 s is in view
        const int outX = int(6 + 300.0 * 2000 / 3000);   // the track spans x 6 to 306
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(outX + 3, 30));
        QVERIFY2(timeline.trimming(), "a press on the out grip seeked instead");
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(outX + 3, 30));
    }
    void zoomLaneGrowsTheTimelineUnderTheFilmStrip() {
        VideoTimeline timeline;
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH);
        QVERIFY(timeline.zoomLaneRect().isEmpty());
        timeline.setZoomLaneVisible(true);
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH + VideoTimeline::kLaneStep);
        QCOMPARE(timeline.zoomLaneRect().top(), qreal(VideoTimeline::kBaseH + theme::kS1));
        QCOMPARE(timeline.zoomLaneRect().height(), 28.0);
        QCOMPARE(timeline.zoomLaneRect().bottom(), qreal(timeline.height()));   // nothing empty under it
        timeline.setZoomLaneVisible(false);
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH);
    }
    void aClickWithoutMovementIsAClick() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.show();
        QSignalSpy clicks(&timeline, &VideoTimeline::stripClicked);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 36));
        QCOMPARE(clicks.count(), 1);
        QCOMPARE(clicks.first().first().toLongLong(), 5000);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 36));
        QTest::mouseMove(&timeline, QPoint(160, 36));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(160, 36));
        QCOMPARE(clicks.count(), 1);   // a scrub
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(100, 36));
        QTest::mouseMove(&timeline, QPoint(102, 36));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(102, 36));
        QCOMPARE(clicks.count(), 2);   // a hand's tremor is still a click
    }
    // C1: a timeline 312 px wide shows 10 s on 300 px of track: x = 6 + t * 0.03.
    void shiftDragMarksARangeAndThePlayheadFollows() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.show();
        QSignalSpy seeks(&timeline, &VideoTimeline::seekRequested);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(66, 36));
        QTest::mouseMove(&timeline, QPoint(126, 36));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(126, 36));
        QVERIFY(timeline.hasMark());
        QCOMPARE(timeline.markRange(), (std::pair<qint64, qint64>{2000, 4000}));
        QCOMPARE(timeline.position(), 4000);   // the frame where the cut would end
        QCOMPARE(seeks.last().first().toLongLong(), 4000);
        QVERIFY(!timeline.interacting());
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(200, 36));
        QVERIFY(!timeline.hasMark());
    }
    void shiftClickMarksFromThePlayhead() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.setMinimumRange(33);
        timeline.show();
        timeline.setPosition(1000);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(96, 36));
        QCOMPARE(timeline.markRange(), (std::pair<qint64, qint64>{1000, 3000}));
        QCOMPARE(timeline.position(), 3000);
        timeline.clearMark();
        timeline.setPosition(2000);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(66, 36));   // no width
        QVERIFY(!timeline.hasMark());
    }
    void escapeRestoresTheMarkBeforeTheDrag() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.show();
        timeline.setPosition(500);
        timeline.setMark(1000, 2000);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(156, 36));
        QTest::mouseMove(&timeline, QPoint(216, 36));
        QVERIFY(timeline.marking());
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QCOMPARE(timeline.markRange(), (std::pair<qint64, qint64>{1000, 2000}));
        QCOMPARE(timeline.position(), 500);
        QVERIFY(!timeline.interacting());
    }
    void aMarkStaysInsideTheTrimAndShiftAtAGripStillTrims() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.setTrimRange(3000, 10000);
        timeline.show();
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(66, 36));
        QTest::mouseMove(&timeline, QPoint(126, 36));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(126, 36));
        QCOMPARE(timeline.markRange(), (std::pair<qint64, qint64>{3000, 4000}));
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(93, 36));   // the In grip
        QVERIFY(timeline.trimming() && !timeline.marking());
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(93, 36));
    }
    void anOpenMarkFollowsThePlayhead() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.setPosition(2000);
        timeline.markFromPlayhead();
        timeline.setPosition(3000);
        QCOMPARE(timeline.markRange(), (std::pair<qint64, qint64>{2000, 3000}));
        timeline.markToPlayhead();
        timeline.setPosition(5000);
        QCOMPARE(timeline.markRange(), (std::pair<qint64, qint64>{2000, 3000}));
    }
    void aMarkLiftsTheStripOneStepAndFadesOnTheFastSpring() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.show();
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const auto lift = [&] {
            const QImage shot = timeline.grab().toImage();
            return std::abs(qGray(shot.pixel(96, 36)) - qGray(shot.pixel(216, 36)));
        };
        QCOMPARE(lift(), 0);
        timeline.setMark(2000, 4000);
        const int marked = lift();
        QVERIFY2(marked >= 4 && marked <= 40, qPrintable(QString::number(marked)));
        Spring::setEnabled(true);
        const auto off = qScopeGuard([] { Spring::setEnabled(false); });
        timeline.clearMark();
        QTest::qWait(40);
        const int fading = lift();
        QVERIFY2(fading > 0 && fading < marked, qPrintable(QString::number(fading)));
        QTRY_COMPARE_WITH_TIMEOUT(lift(), 0, 2000);
    }
    void aPlainWheelZoomsAtThePointer() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.show();
        const auto wheel = [&](QPoint angle, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QWheelEvent event(QPointF(156, 36), timeline.mapToGlobal(QPointF(156, 36)), QPoint(), angle,
                              Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&timeline, &event);
        };
        wheel(QPoint(0, 120));
        QVERIFY(qAbs(timeline.visibleEnd() - timeline.visibleStart() - 8333) <= 1);
        QVERIFY(qAbs(timeline.visibleStart() - 833) <= 1);   // 5 s stays under the pointer
        const qint64 start = timeline.visibleStart();
        wheel(QPoint(0, -120), Qt::ShiftModifier);
        QVERIFY(qAbs(timeline.visibleStart() - (start + 833)) <= 1);   // Shift pans
        wheel(QPoint(120, 0));                                         // so does a sideways swipe
        QVERIFY(qAbs(timeline.visibleStart() - start) <= 1);
    }
    void aPressOnTheEmptyLaneWaitsForTheRelease() {
        VideoTimeline timeline;
        timeline.resize(312, 84);
        timeline.setDuration(10000);
        timeline.setZoomLaneVisible(true);
        timeline.show();
        QSignalSpy add(&timeline, &VideoTimeline::zoomAddRequested);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(add.count(), 0);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(add.count(), 1);
        QCOMPARE(add.first().first().toLongLong(), 5000);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QTest::mouseMove(&timeline, QPoint(200, 66));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(200, 66));
        QCOMPARE(add.count(), 1);   // a drag adds nothing
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 66));
        QCOMPARE(add.count(), 1);   // nor does one let go after Esc
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
        const int nearTop = int(timeline.zoomLaneRect().top()) + 6;
        QVERIFY(qGray(shot.pixel(156, nearTop)) != qGray(shot.pixel(72, nearTop)));
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
        // Ruler ticks count edited time at even spacing, up to its 6 s.
        timeline.resize(612, 52);                           // 1 s steps, some past the cut
        const auto ticks = timeline.rulerTicks();
        QCOMPARE(ticks.size(), 5);
        for (int i = 0; i < ticks.size(); ++i) {
            QCOMPARE(ticks[i].second, (i + 1) * 1000);
            QVERIFY(qAbs(ticks[i].first - (6 + 600.0 * (i + 1) / 6)) < 0.01);
        }
        timeline.resize(312, 52);
        // Without fragments the axis is the source again.
        timeline.setFragments({});
        seeks.clear();
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(156, 36));
        QCOMPARE(seeks.last().first().toLongLong(), 5000);
    }
    void cuttingBeforeTheViewKeepsItOnTheSameSource() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        timeline.zoomAt(5, 6000);
        const qint64 start = timeline.visibleStart(), end = timeline.visibleEnd();
        QVERIFY(end - start < 3000);
        timeline.setFragments({{0, 1.0, false}, {1000, 1.0, true}, {2000, 1.0, false}});
        QVERIFY(qAbs(timeline.visibleStart() - start) <= 2);
        QVERIFY(qAbs(timeline.visibleEnd() - end) <= 2);
    }
    void maskLaneSelectsAndResizesWindows() {
        VideoTimeline timeline;
        timeline.resize(312, 52);
        timeline.setDuration(10000);
        QVERIFY(timeline.maskLaneRect().isEmpty());
        timeline.setMasks({{7, 2000, 4000, QStringLiteral("Blur"), false}});
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH + VideoTimeline::kMaskStep);
        QCOMPARE(timeline.maskLaneRect().top(), qreal(VideoTimeline::kBaseH + theme::kS1));
        timeline.show();
        QSignalSpy selected(&timeline, &VideoTimeline::maskSelected);
        QSignalSpy edited(&timeline, &VideoTimeline::maskWindowEdited);
        const int y = int(timeline.maskLaneRect().center().y());
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(126, y));   // end edge at 4 s
        QCOMPARE(selected.count(), 1);
        QCOMPARE(selected.first().first().value<quintptr>(), quintptr(7));
        QTest::mouseMove(&timeline, QPoint(160, y));
        QTest::mouseMove(&timeline, QPoint(186, y));
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(186, y));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(edited.first().at(2).toLongLong(), 6000);   // (key, from, to, before from, before to)
        QCOMPARE(edited.first().at(4).toLongLong(), 4000);
        // Esc during a drag puts it back.
        timeline.setMasks({{7, 2000, 6000, QStringLiteral("Blur"), true}});
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(126, y));   // the body
        QTest::mouseMove(&timeline, QPoint(160, y));
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(160, y));
        QCOMPARE(edited.count(), 1);
        timeline.setMasks({});
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH);
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

    void waveformLaneSitsUnderTheFilmStripAndScrubs() {
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("clicks.mkv"));
        QProcess make;
        make.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", "color=c=black:s=64x48:d=3:r=25",
            "-f", "lavfi", "-i", "aevalsrc=exprs=if(between(t\\,1\\,1.02)\\,0.9\\,0):s=48000:d=3",
            "-c:a", "pcm_s16le", file});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        VideoTimeline timeline;
        timeline.resize(612, 52);
        timeline.setDuration(3000);
        timeline.setZoomLaneVisible(true);
        timeline.show();
        const qreal zoomTop = timeline.zoomLaneRect().top();
        QVERIFY(timeline.waveformRect().isEmpty());
        AudioWaveformProvider waveform(file, 3000, 0);
        timeline.setWaveform(&waveform);
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH + VideoTimeline::kWaveStep + VideoTimeline::kLaneStep);
        const QRectF lane = timeline.waveformRect();
        QCOMPARE(lane.height(), 20.0);   // a mask lane's height: no text to carry
        QCOMPARE(lane.top(), qreal(VideoTimeline::kBaseH + theme::kS1));  // right under the film strip
        QCOMPARE(timeline.zoomLaneRect().top(), lane.bottom() + 4);
        QTRY_VERIFY(waveform.state() == AudioWaveformProvider::State::Ready);
        auto xAt = [&](qint64 ms) { return int(lane.left() + lane.width() * ms / 3000.0); };
        const QImage shot = timeline.grab().toImage();
        const int y = int(lane.center().y()) - 4;
        QVERIFY2(shot.pixelColor(xAt(1005), y) != shot.pixelColor(xAt(2000), y),
                 qPrintable(shot.pixelColor(xAt(1005), y).name() + " " + shot.pixelColor(xAt(2000), y).name()));
        // With the click's stretch cut out, the column at the cut shows none of it.
        timeline.setFragments({{0, 1.0, false}, {800, 1.0, true}, {1200, 1.0, false}});
        timeline.fitClip();
        const QImage cut = timeline.grab().toImage();
        const int seam = int(lane.left() + lane.width() * 800 / 2600.0);
        const QColor quiet = cut.pixelColor(int(lane.left() + lane.width() * 400 / 2600.0), y);
        for (int x = seam - 2; x <= seam + 2; ++x)
            QVERIFY2(cut.pixelColor(x, y) == quiet, qPrintable(QStringLiteral("%1: %2").arg(x).arg(cut.pixelColor(x, y).name())));
        timeline.setFragments({});
        timeline.fitClip();
        // A click on the waveform scrubs like the film strip.
        QSignalSpy seeks(&timeline, &VideoTimeline::seekRequested);
        QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(2000), int(lane.center().y())));
        QVERIFY(!seeks.isEmpty());
        QVERIFY(qAbs(seeks.last().first().toLongLong() - 2000) <= 10);
        // Hidden, the lanes below move back up.
        timeline.setWaveformVisible(false);
        QCOMPARE(timeline.zoomLaneRect().top(), zoomTop);
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH + VideoTimeline::kLaneStep);
    }
    void aSilentTrackFoldsItsLaneAway() {
        // Screen recordings often carry a track of pure silence: nothing to show, no lane.
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("silent.mkv"));
        QProcess make;
        make.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", "color=c=black:s=64x48:d=2:r=25",
            "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo:d=2", "-c:a", "pcm_s16le", "-shortest", file});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        VideoTimeline timeline;
        timeline.setDuration(2000);
        AudioWaveformProvider waveform(file, 2000, 0);
        timeline.setWaveform(&waveform);
        QVERIFY(timeline.waveformShown());   // while it is read, the lane stays
        QTRY_VERIFY(waveform.state() == AudioWaveformProvider::State::Ready);
        QVERIFY(waveform.silent());
        QVERIFY(!timeline.waveformShown());
        QCOMPARE(timeline.height(), VideoTimeline::kBaseH);
    }
    void quietSoundStillShowsInTheWaveform() {
        // Screen recordings sit far below full scale; on a linear scale a tone
        // at -40 dB was a flat line.
        if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("quiet.mkv"));
        QProcess make;
        make.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", "color=c=black:s=64x48:d=3:r=25",
            "-f", "lavfi", "-i", "aevalsrc=exprs=if(between(t\\,1\\,2)\\,0.01*sin(2*PI*440*t)\\,0):s=48000:d=3",
            "-c:a", "pcm_s16le", file});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        VideoTimeline timeline;
        timeline.resize(612, 52);
        timeline.setDuration(3000);
        AudioWaveformProvider waveform(file, 3000, 0);
        timeline.setWaveform(&waveform);
        QTRY_VERIFY(waveform.state() == AudioWaveformProvider::State::Ready);
        const QRectF lane = timeline.waveformRect();
        auto xAt = [&](qint64 ms) { return int(lane.left() + lane.width() * ms / 3000.0); };
        const QImage shot = timeline.grab().toImage();
        const int y = int(lane.center().y()) - 3;
        QVERIFY2(shot.pixelColor(xAt(1500), y) != shot.pixelColor(xAt(2500), y),
                 qPrintable(shot.pixelColor(xAt(1500), y).name()));
    }
    void aMovingPlayheadRepaintsOnlyItsColumns() {
        // Playback moves the playhead with every frame; the film strip, the
        // lanes and the ruler stay as they are.
        VideoTimeline timeline;
        timeline.resize(612, 84);
        timeline.setDuration(10000);
        timeline.show();
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        timeline.setPosition(2000);
        QCoreApplication::processEvents();
        struct Spy : QObject {
            QRegion painted;
            bool eventFilter(QObject *, QEvent *e) override {
                if (e->type() == QEvent::Paint) painted += static_cast<QPaintEvent *>(e)->region();
                return false;
            }
        } spy;
        timeline.installEventFilter(&spy);
        timeline.setPosition(6000);
        const int from = 6 + 600 * 2 / 10, to = 6 + 600 * 6 / 10;   // the film strip spans x 6 to 606
        QTRY_VERIFY(!spy.painted.isEmpty());
        QVERIFY(spy.painted.contains(QPoint(from, 40)));
        QVERIFY(spy.painted.contains(QPoint(to, 40)));
        QVERIFY(!spy.painted.contains(QPoint((from + to) / 2, 40)));
    }
    void anEditKeepsTheStripUntilNewThumbnailsArrive() {
        // A cut, a zoom or a resize asks for new thumbnails; meanwhile each slot
        // shows the nearest one it already has instead of an empty strip.
        VideoTimeline timeline;
        timeline.resize(612, 52);
        timeline.setDuration(4000);
        QImage red(128, 72, QImage::Format_RGB32);
        red.fill(Qt::red);
        for (qint64 t : timeline.thumbnailTimes()) timeline.setThumbnail(t, red);
        const int y = 33;   // the film strip spans x 6 to 606, y 18 to 48
        QCOMPARE(timeline.grab().toImage().pixelColor(18, y), QColor(Qt::red));
        timeline.setFragments({{0, 1.0, false}, {1000, 1.0, true}, {2000, 1.0, false}});
        timeline.resize(700, 52);
        const QImage shot = timeline.grab().toImage();
        QCOMPARE(shot.pixelColor(18, y), QColor(Qt::red));
        QCOMPARE(shot.pixelColor(680, y), QColor(Qt::red));
    }
};

QTEST_MAIN(TestVideoTimeline)
#include "test_videotimeline.moc"

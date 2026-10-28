// K, G: the timeline's clip model. Every kept piece is a block, 4 px from the next; what is
// cut stays as a ghost where it was; every block edge pulls; the edit lands on release as
// one step and nothing else moves.
#include "../editor/editortest.h"
#include "app/spring.h"
#include "timeline/bars/fragmentbar.h"
#include <QScopeGuard>
#include <QMenu>
#include <QTimer>

namespace {

// 412 px wide: the track runs x 6 to 406, 400 px.
void place(VideoTimeline &timeline) {
    timeline.resize(412, VideoTimeline::kBaseH);
    timeline.show();
}

// Keep 0-4 s, cut 4-6 s, keep 6-10 s: the whole 10 s on the strip, 40 px a second, the cut a
// ghost at x 166-246. The first block's end grip has its middle at x 161, the second's
// start grip at 251.
QVector<Fragment> withCut() { return {{0, 1.0, false}, {4000, 1.0, true}, {6000, 1.0, false}}; }

void pull(VideoTimeline &timeline, int from, int to, bool release = true) {
    QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(from, 36));
    QTest::mouseMove(&timeline, QPoint((from + to) / 2, 36));
    QTest::mouseMove(&timeline, QPoint(to, 36));
    if (release) QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(to, 36));
}

}

class TestClips : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { Spring::setEnabled(false); }

    void everyPieceIsABlockAndACutAGhostBetween() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        const QVector<QRectF> blocks = timeline.pieceRects();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].left(), 6.0 + theme::kS1 / 2);   // the first starts at the track
        QCOMPARE(blocks[0].right(), 166.0 - theme::kS1 / 2);
        QCOMPARE(blocks[1].left(), 246.0 + theme::kS1 / 2);
        QCOMPARE(blocks[1].right(), 406.0 - theme::kS1 / 2);
        QCOMPARE(timeline.ghostRects(), (QVector<QRectF>{QRectF(168, 20, 76, 32)}));
        // Measured on the render: the gap's middle column is the bar, block and ghost either side are not.
        const QImage shot = timeline.grab().toImage();
        const QColor bar = timeline.palette().color(QPalette::Window);
        for (const int seam : {166, 246}) {
            QCOMPARE(shot.pixelColor(seam, 36), bar);
            QVERIFY(shot.pixelColor(seam - 4, 36) != bar);
            QVERIFY(shot.pixelColor(seam + 4, 36) != bar);
        }
        // A split without a cut: two blocks 4 px apart, no ghost.
        timeline.setFragments({{0, 1.0, false}, {5000, 1.0, false}});
        QCOMPARE(timeline.pieceRects()[1].left() - timeline.pieceRects()[0].right(), qreal(theme::kS1));
        QVERIFY(timeline.ghostRects().isEmpty());
        // A clip never split is one block; trimmed ends are ghosts and the view keeps them.
        timeline.setFragments({});
        timeline.setTrimRange(2000, 8000);
        QCOMPARE(timeline.pieceRects().size(), 1);
        QCOMPARE(timeline.ghostRects().size(), 2);
        QCOMPARE(timeline.visibleEnd() - timeline.visibleStart(), qint64(10000));
    }

    void anInnerEdgeCutsWhatItLeavesAndTakesItBack() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        const QRectF second = timeline.pieceRects()[1];
        // 40 px in is a second shorter; the ghost takes it, the second block stays put.
        pull(timeline, 161, 121);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(edits.last().at(2).toInt(), 0);   // the piece that was pulled
        const fragments::Clip shorter = timeline.clip();
        QCOMPARE(shorter.list, (QVector<Fragment>{{0, 1.0, false}, {3000, 1.0, true}, {6000, 1.0, false}}));
        QCOMPARE(shorter.inMs, qint64(0));
        QCOMPARE(timeline.pieceRects()[1], second);
        QCOMPARE(timeline.ghostRects()[0].left(), 126.0 + theme::kS1 / 2);
        // Out again, it takes back no more than lies before the next piece.
        const qreal end = timeline.pieceRects()[0].right();
        pull(timeline, int(end) - 3, 406);
        QCOMPARE(timeline.clip().list, (QVector<Fragment>{{0, 1.0, false}, {6000, 1.0, false}}));
        QCOMPARE(timeline.pieceRects().size(), 2);   // a split stays a split
        QCOMPARE(timeline.pieceRects()[1], second);
        QVERIFY(timeline.ghostRects().isEmpty());
    }

    void thePulledEdgeFollowsThePointerAndEscPutsItBack() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        QSignalSpy seeks(&timeline, &VideoTimeline::seekRequested);
        const QVector<QRectF> before = timeline.pieceRects();
        // The second block's start, pulled 60 px right: a second and a half given up.
        pull(timeline, 251, 311, false);
        QVERIFY(timeline.trimming());
        const QVector<QRectF> during = timeline.pieceRects();
        QVERIFY2(qAbs(during[1].left() - (before[1].left() + 60)) <= 0.5, qPrintable(QString::number(during[1].left())));
        QVERIFY(qAbs(timeline.ghostRects()[0].right() - (before[1].left() + 60 - theme::kS1)) <= 0.5);   // the ghost grows with it
        QCOMPARE(during[1].right(), before[1].right());   // only the pulled edge moves
        QCOMPARE(during[0], before[0]);
        QCOMPARE(seeks.last().first().toLongLong(), 7500);   // the canvas shows the new first frame
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(311, 36));
        QCOMPARE(edits.count(), 0);
        QCOMPARE(timeline.clip().list, withCut());
        QCOMPARE(timeline.pieceRects(), before);
    }

    // G: shortening moves nothing else; closing a pause does, and the blocks glide there.
    void theBlocksGlideWhenAPauseCloses() {
        Spring::setEnabled(true);
        const auto off = qScopeGuard([] { Spring::setEnabled(false); });
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments({{0, 1.0, false}, {4000, 1.0, true}, {6000, 1.0, false, 2000}});
        place(timeline);
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const qreal before = timeline.pieceRects()[1].left();
        QTest::mouseDClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, timeline.pauseRect(1).center().toPoint());
        QCOMPARE(timeline.clip().list, withCut());
        // At once the second block still stands where it was, then glides to its place.
        const qreal atRelease = timeline.pieceRects()[1].left();
        QVERIFY2(qAbs(atRelease - before) <= 0.5, qPrintable(QStringLiteral("%1 %2").arg(atRelease).arg(before)));
        QTest::qWait(60);
        const qreal midway = timeline.pieceRects()[1].left();
        QTest::qWait(700);
        const qreal settled = timeline.pieceRects()[1].left();
        qInfo("second block: %.1f at release, %.1f after 60 ms, %.1f settled", atRelease, midway, settled);
        QVERIFY(qAbs(settled - atRelease) > 10);
        QVERIFY(qMin(atRelease, settled) + 1 < midway && midway < qMax(atRelease, settled) - 1);
    }

    // G: an edit that cuts (not a pull) turns the block into its ghost on `base`, no jump.
    void aCutFadesFromBlockToGhost() {
        Spring::setEnabled(true);
        const auto off = qScopeGuard([] { Spring::setEnabled(false); });
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments({{0, 1.0, false}, {4000, 1.0, false}, {6000, 1.0, false}});
        place(timeline);
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const QPoint at(206, 30);   // inside the middle piece, clear of the grips and the hatch's lines
        const QColor block = timeline.grab().toImage().pixelColor(at);
        timeline.setFragments(withCut());
        const QColor first = timeline.grab().toImage().pixelColor(at);
        QTest::qWait(60);
        const QColor midway = timeline.grab().toImage().pixelColor(at);
        QTest::qWait(500);
        const QColor ghost = timeline.grab().toImage().pixelColor(at);
        qInfo("lightness: block %d, at once %d, after 60 ms %d, settled %d",
              block.lightness(), first.lightness(), midway.lightness(), ghost.lightness());
        // From the block's look down to the ghost's, step by step (the springs run on the clock,
        // so the first grab already lands part way).
        QVERIFY(block.lightness() >= ghost.lightness() + 6);
        QVERIFY(first.lightness() > midway.lightness() && midway.lightness() > ghost.lightness());
        QVERIFY(first.lightness() > ghost.lightness() + (block.lightness() - ghost.lightness()) / 3);
    }

    void withMotionOffTheyLandAtOnce() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments({{0, 1.0, false}, {4000, 1.0, true}, {6000, 1.0, false, 2000}});
        place(timeline);
        const qreal before = timeline.pieceRects()[1].left();
        QTest::mouseDClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, timeline.pauseRect(1).center().toPoint());
        const qreal landed = timeline.pieceRects()[1].left();
        QTest::qWait(100);
        QCOMPARE(timeline.pieceRects()[1].left(), landed);
        QVERIFY(qAbs(landed - before) > 10);
    }

    void anEdgeEditIsOneUndoStep() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-clips.mp4");
        doc.video = {QSize(64, 48), 10000, 25.0};
        Config cfg; cfg.animations = false;
        EditorWindow w(doc, cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        StudioDocument studio = w.studioDocument();
        studio.fragments = withCut();
        w.setStudioDocument(studio);
        const int steps = undo->count();
        // Trim and fragments together, as a pulled first start over a cut makes them.
        emit timeline->clipEdited(timeline->clip(), {1000, 9000, {{0, 1.0, false}, {6000, 1.0, false}}}, 1);
        QCOMPARE(undo->count(), steps + 1);
        QCOMPARE(timeline->trimIn(), 1000);
        QCOMPARE(timeline->trimOut(), 9000);
        QCOMPARE(w.studioDocument().fragments.size(), 2);
        QVERIFY(w.findChild<FragmentBar *>()->isVisibleTo(&w));   // the pulled piece is chosen
        undo->undo();
        QCOMPARE(timeline->trimIn(), 0);
        QCOMPARE(timeline->trimOut(), 10000);
        QCOMPARE(w.studioDocument().fragments, withCut());
    }
    // P: Shift on a block moves it along the output; the pause before it grows. Close it
    // with Shift and a double click, or let it snap back within 8 px; Ctrl places freely.
    void shiftDraggingABlockMakesAPause() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        // The second block (6-10 s, x 248-404), 40 px right: a second of pause after the ghost.
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(300, 36));
        QVERIFY(timeline.movingPiece());
        QMouseEvent move(QEvent::MouseMove, QPointF(340, 36), timeline.mapToGlobal(QPointF(340, 36)),
                         Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
        QApplication::sendEvent(&timeline, &move);
        QVERIFY2(qAbs(timeline.pieceRects()[1].left() - 288) <= 0.5, qPrintable(QString::number(timeline.pieceRects()[1].left())));
        // 40 px of pause, less the 4 px gap to the block.
        QCOMPARE(timeline.pauseRect(1).width(), 40.0 - theme::kS1);
        QCOMPARE(timeline.ghostRects()[0], QRectF(168, 20, 76, 32));   // the ghost stays where it was
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(340, 36));
        QCOMPARE(edits.count(), 1);
        QCOMPARE(timeline.clip().list[2].pauseMs, qint64(1000));
        QCOMPARE(timeline.pieceRects()[1].right(), 406.0 - theme::kS1 / 2);   // fitted again: 11 s
        QCOMPARE(timeline.ghostRects()[0].right() + theme::kS1, timeline.pauseRect(1).left());   // cut, then pause
        // Measured on the render: the pause is the recessed well, not the bar and not a block.
        const QImage shot = timeline.grab().toImage();
        const QColor well = theme::tone(theme::Tone::Well, timeline.palette().color(QPalette::Window).lightness() < 128);
        QCOMPARE(shot.pixelColor(timeline.pauseRect(1).center().toPoint()).rgb(), well.rgb());
        // Shift and a double click in it: closed, one edit.
        QTest::mouseDClick(&timeline, Qt::LeftButton, Qt::ShiftModifier, timeline.pauseRect(1).center().toPoint());
        QCOMPARE(edits.count(), 2);
        QCOMPARE(timeline.clip().list, withCut());
    }

    void aSmallPullSnapsBackUnlessCtrlIsHeld() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        const auto pullBy = [&](int dx, Qt::KeyboardModifiers modifiers) {
            QTest::mousePress(&timeline, Qt::LeftButton, modifiers, QPoint(300, 36));
            QMouseEvent move(QEvent::MouseMove, QPointF(300 + dx, 36), timeline.mapToGlobal(QPointF(300 + dx, 36)),
                             Qt::NoButton, Qt::LeftButton, modifiers);
            QApplication::sendEvent(&timeline, &move);
            QTest::mouseRelease(&timeline, Qt::LeftButton, modifiers, QPoint(300 + dx, 36));
        };
        pullBy(6, Qt::ShiftModifier);                          // 150 ms, under 8 px: back on its neighbour
        QCOMPARE(edits.count(), 0);
        pullBy(6, Qt::ShiftModifier | Qt::ControlModifier);    // Ctrl: no snapping
        QCOMPARE(edits.count(), 1);
        QCOMPARE(timeline.clip().list[2].pauseMs, qint64(150));
    }

    void thePausesMenuTurnsItBlack() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments({{0, 1.0, false}, {4000, 1.0, true}, {6000, 1.0, false, 1000, false}});
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        QStringList seen;
        QTimer::singleShot(300, &timeline, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            QVERIFY(menu);
            for (QAction *a : menu->actions()) seen << a->text();
            for (QAction *a : menu->actions()) if (a->text() == QStringLiteral("Black")) a->trigger();
            menu->close();
        });
        emit timeline.customContextMenuRequested(timeline.pauseRect(1).center().toPoint());
        QVERIFY2(seen.contains(QStringLiteral("Hold last frame")), qPrintable(seen.join(QLatin1Char('|'))));
        QCOMPARE(edits.count(), 1);
        QVERIFY(timeline.clip().list[2].pauseBlack);
        QCOMPARE(timeline.grab().toImage().pixelColor(timeline.pauseRect(1).center().toPoint()).rgb(), QColor(Qt::black).rgb());
    }

    // Alt marks, and on a grip pulls finely; Shift on a grip or at a seam pulls apart (G).
    void altMarksAndTrimsFinelyShiftPullsApartAtASeam() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::AltModifier, QPoint(300, 36));
        QVERIFY(timeline.marking());
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::AltModifier, QPoint(350, 36));
        QVERIFY(timeline.hasMark());
        QTest::mousePress(&timeline, Qt::LeftButton, Qt::AltModifier, QPoint(251, 36));   // the second block's start grip
        QVERIFY(timeline.trimming() && !timeline.marking());
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::AltModifier, QPoint(251, 36));
        // A split, no cut: the seam at x 206. Shift on either grip there moves the right piece.
        timeline.setFragments({{0, 1.0, false}, {5000, 1.0, false}});
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        for (const int grip : {203, 209}) {
            QTest::mousePress(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(grip, 36));
            QVERIFY(timeline.movingPiece());
            QMouseEvent move(QEvent::MouseMove, QPointF(grip + 40, 36), timeline.mapToGlobal(QPointF(grip + 40, 36)),
                             Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
            QApplication::sendEvent(&timeline, &move);
            QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::ShiftModifier, QPoint(grip + 40, 36));
            QCOMPARE(timeline.clip().list[1].pauseMs, qint64(1000));
            QCOMPARE(timeline.clip().list[0].pauseMs, qint64(0));
            timeline.setFragments({{0, 1.0, false}, {5000, 1.0, false}});
        }
        QCOMPARE(edits.count(), 2);
    }
};

QTEST_MAIN(TestClips)
#include "test_clips.moc"

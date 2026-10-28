// K: the timeline's clip model. Every kept piece is a block, 4 px from the next; every
// block edge pulls; the edit lands on release as one step and the blocks glide there.
#include "../editor/editortest.h"
#include "app/spring.h"
#include "timeline/bars/fragmentbar.h"
#include <QScopeGuard>

namespace {

// 412 px wide: the track runs x 6 to 406, 400 px.
void place(VideoTimeline &timeline) {
    timeline.resize(412, VideoTimeline::kBaseH);
    timeline.show();
}

// Keep 0-4 s, cut 4-6 s, keep 6-10 s: 8 s of output, 50 px a second. The first block's
// end grip has its middle at x 201, the second's start grip at 211.
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

    void everyPieceIsABlockFourPixelsFromTheNext() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        const QVector<QRectF> blocks = timeline.pieceRects();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[1].left() - blocks[0].right(), qreal(theme::kS1));
        QCOMPARE(blocks[0].left(), 6.0 + theme::kS1 / 2);   // the first starts at the track
        QCOMPARE(blocks[1].right(), 406.0 - theme::kS1 / 2);
        // Measured on the render: the gap's middle column is the bar, the blocks either side are not.
        const QImage shot = timeline.grab().toImage();
        const QColor bar = timeline.palette().color(QPalette::Window);
        const int seam = qRound((blocks[0].right() + blocks[1].left()) / 2);
        QCOMPARE(shot.pixelColor(seam, 36), bar);
        QVERIFY(shot.pixelColor(seam - 4, 36) != bar);
        QVERIFY(shot.pixelColor(seam + 4, 36) != bar);
        // A clip never split is one block, trimmed ends are not on the strip.
        timeline.setFragments({});
        timeline.setTrimRange(2000, 8000);
        QCOMPARE(timeline.pieceRects().size(), 1);
        QCOMPARE(timeline.visibleEnd() - timeline.visibleStart(), qint64(6000));
    }

    void anInnerEdgeCutsWhatItLeavesAndTakesItBack() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        // 50 px in is a second shorter.
        pull(timeline, 201, 151);
        QCOMPARE(edits.count(), 1);
        QCOMPARE(edits.last().at(2).toInt(), 0);   // the piece that was pulled
        const fragments::Clip shorter = timeline.clip();
        QCOMPARE(shorter.list, (QVector<Fragment>{{0, 1.0, false}, {3000, 1.0, true}, {6000, 1.0, false}}));
        QCOMPARE(shorter.inMs, qint64(0));
        // Out again, it takes back no more than lies before the next piece.
        const qreal end = timeline.pieceRects()[0].right();
        pull(timeline, int(end) - 3, 406);
        QCOMPARE(timeline.clip().list, (QVector<Fragment>{{0, 1.0, false}, {6000, 1.0, false}}));
        QCOMPARE(timeline.pieceRects().size(), 2);   // a split stays a split
    }

    void thePulledEdgeFollowsThePointerAndEscPutsItBack() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QSignalSpy edits(&timeline, &VideoTimeline::clipEdited);
        QSignalSpy seeks(&timeline, &VideoTimeline::seekRequested);
        const QVector<QRectF> before = timeline.pieceRects();
        // The second block's start, pulled 75 px right: a second and a half given up.
        pull(timeline, 211, 286, false);
        QVERIFY(timeline.trimming());
        const QVector<QRectF> during = timeline.pieceRects();
        QVERIFY2(qAbs(during[1].left() - (before[1].left() + 75)) <= 0.5, qPrintable(QString::number(during[1].left())));
        QCOMPARE(during[1].right(), before[1].right());   // only the pulled edge moves
        QCOMPARE(during[0], before[0]);
        QCOMPARE(seeks.last().first().toLongLong(), 7500);   // the canvas shows the new first frame
        QTest::keyClick(&timeline, Qt::Key_Escape);
        QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(286, 36));
        QCOMPARE(edits.count(), 0);
        QCOMPARE(timeline.clip().list, withCut());
        QCOMPARE(timeline.pieceRects(), before);
    }

    void theBlocksGlideFromWhereTheEditLeftThem() {
        Spring::setEnabled(true);
        const auto off = qScopeGuard([] { Spring::setEnabled(false); });
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        QVERIFY(QTest::qWaitForWindowExposed(&timeline));
        const qreal before = timeline.pieceRects()[1].left();
        pull(timeline, 201, 151);   // a second off the first block
        // At release the second block still stands where it was, then glides to its place.
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

    void withMotionOffTheyLandAtOnce() {
        VideoTimeline timeline;
        timeline.setDuration(10000);
        timeline.setFragments(withCut());
        place(timeline);
        const qreal before = timeline.pieceRects()[1].left();
        pull(timeline, 201, 151);
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
};

QTEST_MAIN(TestClips)
#include "test_clips.moc"

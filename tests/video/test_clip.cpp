// K: the clip model's edits. Trim and fragments are one Clip; every piece edge moves,
// a piece goes, and a split stays a split.
#include <QtTest>
#include "video/fragments.h"

using namespace eddy;
using namespace eddy::fragments;

namespace {

constexpr qint64 kDur = 10000;

// Pieces as source [start, end) pairs, for compact comparisons.
QVector<std::pair<qint64, qint64>> spans(const Clip &clip) {
    QVector<std::pair<qint64, qint64>> out;
    for (const TimePiece &p : clip.pieces(kDur)) out.append({qint64(p.srcStart), qint64(p.srcEnd)});
    return out;
}

Clip whole() { return Clip{0, kDur, {}}; }

// 0..3 s kept, 3..5 s cut, 5..10 s kept.
Clip withCut() { return Clip{0, kDur, {{0, 1.0, false}, {3000, 1.0, true}, {5000, 1.0, false}}}; }

// Split at 4 s, nothing cut: two pieces touching in the source.
Clip withSplit() { return Clip{0, kDur, {{0, 1.0, false}, {4000, 1.0, false}}}; }

}

class TestClip : public QObject {
    Q_OBJECT
private slots:
    void outerEdgesAreTheTrim() {
        Clip c = moveStart(whole(), 0, 1500, kDur);
        QCOMPARE(c.inMs, qint64(1500));
        QVERIFY(c.list.isEmpty());   // no fragment for a trim
        c = moveEnd(c, 0, 8000, kDur);
        QCOMPARE(c.outMs, qint64(8000));
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{1500, 8000}}));
        c = moveStart(c, 0, -500, kDur);   // clamped to the file
        QCOMPARE(c.inMs, qint64(0));
        c = moveEnd(c, 0, 0, kDur);        // at least kMinFragmentMs stays
        QCOMPARE(c.outMs, qint64(kMinFragmentMs));
    }

    void anInnerEdgeCutsWhatItLeaves() {
        // Pulling the first piece's end in from 3 s to 2 s cuts 2..5 s in one fragment.
        Clip c = moveEnd(withCut(), 0, 2000, kDur);
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{0, 2000}, {5000, kDur}}));
        QCOMPARE(c.list, (QVector<Fragment>{{0, 1.0, false}, {2000, 1.0, true}, {5000, 1.0, false}}));
        // The second piece's start in from 5 s to 6 s: the cut grows to 6 s.
        c = moveStart(c, 1, 6000, kDur);
        QCOMPARE(c.list, (QVector<Fragment>{{0, 1.0, false}, {2000, 1.0, true}, {6000, 1.0, false}}));
        QCOMPARE(c.inMs, qint64(0));
    }

    void anInnerEdgeTakesBackUpToTheNextPiece() {
        Clip c = moveEnd(withCut(), 0, 4000, kDur);
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{0, 4000}, {5000, kDur}}));
        c = moveEnd(withCut(), 0, 9000, kDur);   // only up to where the next piece starts
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{0, 5000}, {5000, kDur}}));
        QCOMPARE(c.list, (QVector<Fragment>{{0, 1.0, false}, {5000, 1.0, false}}));   // still two pieces
        c = moveStart(withCut(), 1, 3500, kDur);
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{0, 3000}, {3500, kDur}}));
        c = moveStart(withCut(), 1, 0, kDur);
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{0, 3000}, {3000, kDur}}));
    }

    void aSplitCannotGrowButCanShrink() {
        QCOMPARE(endRange(withSplit(), 0, kDur), (std::pair<qint64, qint64>{kMinFragmentMs, 4000}));
        QCOMPARE(moveEnd(withSplit(), 0, 6000, kDur), withSplit());
        const Clip c = moveStart(withSplit(), 1, 4500, kDur);
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{0, 4000}, {4500, kDur}}));
        // Taking it back keeps the split: two kept pieces of the same speed stay two.
        QCOMPARE(moveStart(c, 1, 4000, kDur), withSplit());
    }

    void takenBackSourcePlaysAtThePiecesSpeed() {
        Clip c = withCut();
        c.list[2].speed = 2.0;
        c = moveStart(c, 1, 4000, kDur);
        QCOMPARE(c.list, (QVector<Fragment>{{0, 1.0, false}, {3000, 1.0, true}, {4000, 2.0, false}}));
        // And the first piece's end takes the cut back at 1x.
        c = moveEnd(withCut(), 0, 3500, kDur);
        QCOMPARE(c.list, (QVector<Fragment>{{0, 1.0, false}, {3500, 1.0, true}, {5000, 1.0, false}}));
    }

    void extendingTheTrimRestoresEvenACut() {
        // A cut that lay outside the trim is kept again when the trim reaches over it.
        Clip c{6000, kDur, {{0, 1.0, false}, {2000, 1.0, true}, {4000, 1.0, false}}};
        c = moveStart(c, 0, 1000, kDur);
        QCOMPARE(c.inMs, qint64(1000));
        QCOMPARE(spans(c), (QVector<std::pair<qint64, qint64>>{{1000, kDur}}));   // one piece swallows it
        // Same at the end.
        Clip e{0, 3000, {{0, 1.0, false}, {5000, 1.0, true}, {7000, 1.0, false}}};
        e = moveEnd(e, 0, kDur, kDur);
        QCOMPARE(e.outMs, kDur);
        QCOMPARE(spans(e), (QVector<std::pair<qint64, qint64>>{{0, kDur}}));
    }

    void removingAPieceClosesTheGap() {
        // Middle piece: cut, melting with the cuts around it.
        Clip three{0, kDur, {{0, 1.0, false}, {2000, 1.0, true}, {4000, 1.0, false}, {6000, 1.0, true}, {8000, 1.0, false}}};
        const auto middle = removePiece(three, 1, kDur);
        QVERIFY(middle);
        QCOMPARE(middle->list, (QVector<Fragment>{{0, 1.0, false}, {2000, 1.0, true}, {8000, 1.0, false}}));
        // First and last move the trim.
        QCOMPARE(removePiece(three, 0, kDur)->inMs, qint64(4000));
        QCOMPARE(removePiece(three, 2, kDur)->outMs, qint64(6000));
        // The only piece stays.
        QVERIFY(!removePiece(whole(), 0, kDur));
        // A split piece goes on its own; its neighbour stays.
        const auto right = removePiece(withSplit(), 1, kDur);
        QCOMPARE(spans(*right), (QVector<std::pair<qint64, qint64>>{{0, 4000}}));
    }

    void badIndicesChangeNothing() {
        QCOMPARE(moveStart(withCut(), 5, 100, kDur), withCut());
        QCOMPARE(moveEnd(withCut(), -1, 100, kDur), withCut());
        QVERIFY(!removePiece(withCut(), 7, kDur));
    }
    // P: pauses stay before their piece through every edit.
    void aPauseStaysWithItsPiece() {
        Clip c = setPause(withCut(), 1, 1500, kDur);
        QCOMPARE(c.list[2].pauseMs, qint64(1500));
        QCOMPARE(c.pieces(kDur)[1].outStart, 4500.0);   // 3 s kept (the cut takes none), then 1.5 s of pause
        QCOMPARE(setPause(c, 1, 0, kDur), withCut());    // closed: as before
        QCOMPARE(setPause(c, 1, 9999999, kDur).list[2].pauseMs, Fragment::kMaxPauseMs);
        // Black only on a pause; closing it forgets black.
        QCOMPARE(setPauseBlack(withCut(), 1, true, kDur), withCut());
        const Clip black = setPauseBlack(c, 1, true, kDur);
        QVERIFY(black.list[2].pauseBlack);
        QVERIFY(!setPause(black, 1, 0, kDur).list[2].pauseBlack);
        // Pulling the start in moves the pause to where the piece now starts.
        const Clip pulled = moveStart(c, 1, 6000, kDur);
        QCOMPARE(pulled.pieces(kDur)[1].srcStart, 6000.0);
        QCOMPARE(pulled.pieces(kDur)[1].pauseBefore, 1500.0);
        // Taking source back keeps it too.
        QCOMPARE(moveStart(c, 1, 4000, kDur).pieces(kDur)[1].pauseBefore, 1500.0);
        // A split keeps it before the left part; the right part has none.
        QVector<Fragment> list = c.list;
        QVERIFY(split(list, 8000, kDur));
        const QVector<TimePiece> parts = Clip{0, kDur, list}.pieces(kDur);
        QCOMPARE(parts[1].pauseBefore, 1500.0);
        QCOMPARE(parts[2].pauseBefore, 0.0);
        // Cutting a range inside the piece leaves the pause on its start alone.
        QVERIFY(cutRange(list, 6000, 7000, kDur));
        const QVector<TimePiece> cut = Clip{0, kDur, list}.pieces(kDur);
        QCOMPARE(cut[1].pauseBefore, 1500.0);
        QCOMPARE(cut[2].pauseBefore, 0.0);
    }
    void aPauseKeepsTwoPiecesFromMelting() {
        // Restoring a cut melts equal neighbours, but never across a pause.
        Clip c = setPause(withSplit(), 1, 800, kDur);
        QVector<Fragment> list = c.list;
        QVERIFY(setRemoved(list, 1, true));
        QVERIFY(setRemoved(list, 1, false));
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[1].pauseMs, qint64(800));
        // Joining makes one piece: the pause between goes.
        QVERIFY(join(list, 1));
        QVERIFY(list.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestClip)
#include "test_clip.moc"

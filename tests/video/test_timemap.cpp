#include <QtTest>
#include "video/timemap.h"

using namespace eddy;

static Fragment part(qint64 start, double speed = 1, bool removed = false) {
    Fragment f;
    f.startMs = start;
    f.speed = speed;
    f.removed = removed;
    return f;
}

class TestTimeMap : public QObject {
    Q_OBJECT
private slots:
    void trimAloneShiftsTime() {
        const TimeMap map(10000, 2000, 8000, {});
        QCOMPARE(map.outputDurationMs(), 6000.0);
        QVERIFY(!map.toOutput(1999));
        QCOMPARE(*map.toOutput(2000), 0.0);
        QCOMPARE(*map.toOutput(5000), 3000.0);
        QCOMPARE(*map.toOutput(8000), 6000.0);   // the very end is still a place
        QVERIFY(!map.toOutput(8001));
        QCOMPARE(map.toSource(0), 2000.0);
        QCOMPARE(map.toSource(6000), 8000.0);
        QCOMPARE(map.toSource(-10), 2000.0);
        QCOMPARE(map.toOutputAfter(1000), 0.0);
        QCOMPARE(map.toOutputAfter(9000), 6000.0);
    }

    void cutsVanishAndSpeedCompresses() {
        const TimeMap map(10000, 0, 10000, {part(0), part(4000, 1, true), part(6000, 2)});
        QCOMPARE(map.pieces().size(), qsizetype(2));
        QCOMPARE(map.outputDurationMs(), 6000.0);
        QVERIFY(!map.toOutput(5000));
        QCOMPARE(map.toOutputAfter(5000), 4000.0);
        QCOMPARE(*map.toOutput(8000), 5000.0);
        QCOMPARE(*map.toOutput(10000), 6000.0);
        QCOMPARE(map.toSource(3999.5), 3999.5);
        QCOMPARE(map.toSource(4000), 6000.0);   // the seam belongs to the later piece
        QCOMPARE(map.toSource(5000), 8000.0);
    }

    void slowFragmentsStretchInsideTheTrim() {
        const TimeMap map(10000, 1000, 5000, {part(0), part(3000, 0.5)});
        QCOMPARE(map.outputDurationMs(), 6000.0);
        QCOMPARE(*map.toOutput(3000), 2000.0);
        QCOMPARE(*map.toOutput(4000), 4000.0);
        QCOMPARE(map.toSource(5000), 4500.0);
    }

    void roundTripsAndStaysMonotonic() {
        const TimeMap map(20000, 500, 19000, {part(0), part(3000, 1.5), part(7000, 1, true),
                                              part(9000, 0.25), part(10000, 4), part(15000)});
        for (double src = 0; src <= 20000; src += 7.3)
            if (const auto out = map.toOutput(src))
                QVERIFY2(qAbs(map.toSource(*out) - src) < 1e-9, qPrintable(QString::number(src)));
        double previous = -1;
        for (double out = 0; out <= map.outputDurationMs(); out += 3.1) {
            const double src = map.toSource(out);
            QVERIFY2(src >= previous, qPrintable(QString::number(out)));
            previous = src;
        }
    }

    void everythingCutLeavesNothing() {
        const TimeMap map(5000, 0, 5000, {part(0, 1, true)});
        QCOMPARE(map.outputDurationMs(), 0.0);
        QVERIFY(!map.toOutput(100));
        QCOMPARE(map.toSource(100), 0.0);
        QCOMPARE(map.toOutputAfter(100), 0.0);
    }
    void playheadStepsSkipCutsAndStayOnTheGrid() {
        const TimeMap plain(10000, 0, 10000, {});
        QCOMPARE(stepFrames(plain, 10000, 25, 1000, 1), qint64(1040));
        QCOMPARE(stepFrames(plain, 10000, 25, 1000, -1), qint64(960));
        QCOMPARE(stepFrames(plain, 10000, 25, 0, -1), qint64(0));        // clamped at the start
        QCOMPARE(stepFrames(plain, 10000, 25, 9960, 1), qint64(9999));    // and before the end
        const TimeMap long_(20000, 0, 20000, {});
        qint64 at = 0;                                                    // 29.97 fps: no drift
        for (int i = 0; i < 300; ++i) at = stepFrames(long_, 20000, 29.97, at, 1);
        QCOMPARE(at, qRound64(300 * 1000.0 / 29.97));
        const TimeMap cut(10000, 0, 10000, {part(0), part(2000, 1, true), part(5000)});
        QCOMPARE(stepFrames(cut, 10000, 25, 1960, 1), qint64(5000));     // over the cut
        QCOMPARE(stepFrames(cut, 10000, 25, 5000, -1), qint64(1960));    // and back
        const TimeMap fast(10000, 0, 10000, {part(0, 2.0)});
        QCOMPARE(stepOutput(fast, 10000, 25, 0, 1000), qint64(2000));    // a played second at 2x
        const TimeMap slow(10000, 0, 10000, {part(0, 0.5)});
        QCOMPARE(stepOutput(slow, 10000, 25, 2000, 1000), qint64(2520));   // 2500 is frame 62.5: rounds up
        QCOMPARE(stepOutput(cut, 10000, 25, 1500, 1000), qint64(5520));  // 500 before the cut, 500 after
        const QVector<qint64> edges{0, 3000, 5000, 9960};
        QCOMPARE(*nextEdge(plain, edges, 0, 1), qint64(3000));
        QCOMPARE(*nextEdge(plain, edges, 3000, 1), qint64(5000));
        QCOMPARE(*nextEdge(plain, edges, 5000, -1), qint64(3000));
        QVERIFY(!nextEdge(plain, edges, 9960, 1));
        // An edge inside a cut is its seam: 3000 lands on 5000 and counts once with it.
        QCOMPARE(*nextEdge(cut, edges, 0, 1), qint64(5000));
        QCOMPARE(*nextEdge(cut, edges, 5000, 1), qint64(9960));
    }
    // P: a pause belongs to the piece after it and pushes it later in the output.
    void aPauseHoldsTheFrameBeforeIt() {
        Fragment second;
        second.startMs = 4000;
        second.pauseMs = 1000;
        const TimeMap map(10000, 0, 10000, {Fragment(), second});
        QCOMPARE(map.outputDurationMs(), 11000.0);
        QCOMPARE(map.pieces()[1].outStart, 5000.0);
        QCOMPARE(map.pieces()[1].pauseStart(), 4000.0);
        QCOMPARE(map.toSource(4500), 3999.0);            // the last frame of the piece before
        QCOMPARE(map.toSource(5000), 4000.0);            // the next piece's first
        QCOMPARE(map.toSource(3000), 3000.0);
        QCOMPARE(map.toOutputAfter(4000), 5000.0);
        const auto hit = map.pauseAt(4250);
        QVERIFY(hit);
        QCOMPARE(hit->piece, 1);
        QCOMPARE(hit->intoMs, 250.0);
        QCOMPARE(hit->lengthMs, 1000.0);
        QVERIFY(!hit->black);
        QVERIFY(!map.pauseAt(5000));
        QVERIFY(!map.pauseAt(3999));
    }
    void aPauseCanOpenTheOutput() {
        Fragment first;
        first.pauseMs = 500;
        first.pauseBlack = true;
        const TimeMap map(2000, 300, 2000, {first});   // trimmed: the pause stays before what is kept
        QCOMPARE(map.pieces()[0].outStart, 500.0);
        QCOMPARE(map.toSource(200), 300.0);              // nothing before: the first kept frame
        QVERIFY(map.pauseAt(0) && map.pauseAt(0)->black);
        QCOMPARE(map.outputDurationMs(), 2200.0);
    }
};

QTEST_GUILESS_MAIN(TestTimeMap)
#include "test_timemap.moc"

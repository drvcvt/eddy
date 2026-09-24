#include <QtTest>
#include "timemap.h"

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
};

QTEST_GUILESS_MAIN(TestTimeMap)
#include "test_timemap.moc"

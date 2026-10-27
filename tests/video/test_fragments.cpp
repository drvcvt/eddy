#include <QtTest>
#include "video/fragments.h"
#include "video/timemap.h"

using namespace eddy;

class TestFragments : public QObject {
    Q_OBJECT
private slots:
    void splitCutAndSpeedShapeTheOutput() {
        QVector<Fragment> list;
        QVERIFY(fragments::split(list, 4000, 10000));
        QVERIFY(fragments::split(list, 6000, 10000));
        QCOMPARE(list.size(), 3);
        QVERIFY(fragments::setRemoved(list, 1, true));   // cut 4..6 s
        fragments::setSpeed(list, 2, 2.0);               // 6..10 s at 2x
        const TimeMap time(10000, 0, 10000, list);
        QCOMPARE(time.outputDurationMs(), 4000.0 + 2000.0);
        QVERIFY(!time.toOutput(5000));
        QCOMPARE(*time.toOutput(8000), 5000.0);
        QCOMPARE(fragments::indexAt(list, 5000), 1);
        QCOMPARE(fragments::endOf(list, 1, 10000), 6000);
    }
    void refusesTinyPiecesAndAnEmptyOutput() {
        QVector<Fragment> list;
        QVERIFY(!fragments::split(list, 50, 10000));      // too close to the start
        QVERIFY(!fragments::split(list, 9950, 10000));    // too close to the end
        QVERIFY(fragments::split(list, 5000, 10000));
        QVERIFY(!fragments::split(list, 5050, 10000));    // too close to the seam
        QVERIFY(fragments::setRemoved(list, 0, true));
        QVERIFY(!fragments::setRemoved(list, 1, true));   // the last kept one stays
        fragments::setSpeed(list, 1, 9);
        QCOMPARE(list[1].speed, 4.0);
    }
    void restoringAndJoiningGoBackToTheNormalForm() {
        QVector<Fragment> list;
        QVERIFY(fragments::split(list, 5000, 10000));
        QVERIFY(fragments::setRemoved(list, 1, true));
        QVERIFY(fragments::setRemoved(list, 1, false));
        QVERIFY(list.isEmpty());                           // equal neighbours merge again
        QVERIFY(fragments::split(list, 5000, 10000));
        QVERIFY(fragments::join(list, 1));
        QVERIFY(list.isEmpty());
        QVERIFY(!fragments::join(list, 0));
        // Restoring beside a different speed keeps the seam.
        QVERIFY(fragments::split(list, 3000, 10000));
        QVERIFY(fragments::split(list, 6000, 10000));
        fragments::setSpeed(list, 2, 2.0);
        QVERIFY(fragments::setRemoved(list, 1, true));
        QVERIFY(fragments::setRemoved(list, 1, false));
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[1].startMs, 6000);
    }
    void joiningNeverCutsWhatWasKept() {
        QVector<Fragment> list{{0, 1.0, true}, {5000, 1.0, false}};
        QVERIFY(!fragments::join(list, 1));
        QCOMPARE(list.size(), 2);
        QVERIFY(!list[1].removed);
    }
    void pausesAreCutOrSpedUpInOneGo() {
        using List = QVector<Fragment>;
        const auto f = [](qint64 start, double speed, bool removed) {
            Fragment x; x.startMs = start; x.speed = speed; x.removed = removed; return x;
        };
        List list;
        QVERIFY(fragments::applySpans(list, {{2200, 4800}}, 10000, true));
        QCOMPARE(list, (List{f(0, 1, false), f(2200, 1, true), f(4800, 1, false)}));
        list = {};
        QVERIFY(fragments::applySpans(list, {{2200, 4800}, {6000, 7000}}, 10000, false));
        QCOMPARE(list, (List{f(0, 1, false), f(2200, 4, false), f(4800, 1, false), f(6000, 4, false), f(7000, 1, false)}));
        list = {f(0, 1, false), f(5000, 1, false)};                     // 50 ms from a seam: onto it
        QVERIFY(fragments::applySpans(list, {{2000, 4950}}, 10000, true));
        QCOMPARE(list[2].startMs, 5000);
        list = {};
        QVERIFY(!fragments::applySpans(list, {{0, 10000}}, 10000, true));   // nothing kept: refused
        QVERIFY(list.isEmpty());
    }
    void cuttingARangeIsOneEditThatMeltsCuts() {
        const auto f = [](qint64 start, double speed, bool removed) {
            Fragment x;
            x.startMs = start;
            x.speed = speed;
            x.removed = removed;
            return x;
        };
        using List = QVector<Fragment>;
        List list;
        QVERIFY(fragments::cutRange(list, 2000, 4000, 10000));
        QCOMPARE(list, (List{f(0, 1, false), f(2000, 1, true), f(4000, 1, false)}));
        list = {f(0, 1, false), f(3000, 1, true), f(3500, 1, false)};   // a cut inside melts in
        QVERIFY(fragments::cutRange(list, 2000, 5000, 10000));
        QCOMPARE(list, (List{f(0, 1, false), f(2000, 1, true), f(5000, 1, false)}));
        list = {f(0, 1, false), f(2000, 1, true), f(4000, 1, false)};   // a cut beside melts on
        QVERIFY(fragments::cutRange(list, 4000, 6000, 10000));
        QCOMPARE(list, (List{f(0, 1, false), f(2000, 1, true), f(6000, 1, false)}));
        list = {f(0, 1, false), f(2000, 2, false)};                     // speeds stay outside the cut
        QVERIFY(fragments::cutRange(list, 3000, 4000, 10000));
        QCOMPARE(list, (List{f(0, 1, false), f(2000, 2, false), f(3000, 2, true), f(4000, 2, false)}));
        list = {};                                                       // 50 ms from the start: snapped
        QVERIFY(fragments::cutRange(list, 50, 3000, 10000));
        QCOMPARE(list, (List{f(0, 1, true), f(3000, 1, false)}));
        list = {f(0, 1, false), f(5000, 1, false)};                     // 50 ms from a seam: snapped
        QVERIFY(fragments::cutRange(list, 2000, 4950, 10000));
        QCOMPARE(list[1], f(2000, 1, true));
        QCOMPARE(list[2].startMs, 5000);
        list = {};
        QVERIFY(fragments::cutRange(list, 2000, 2040, 10000));          // a frame is enough
        list = {};
        QVERIFY(!fragments::cutRange(list, 0, 10000, 10000));           // nothing kept: refused
        QVERIFY(list.isEmpty());
        QVERIFY(!fragments::cutRange(list, 3000, 3000, 10000));
    }
};

QTEST_GUILESS_MAIN(TestFragments)
#include "test_fragments.moc"

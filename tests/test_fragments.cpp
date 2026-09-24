#include <QtTest>
#include "fragments.h"
#include "timemap.h"

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
};

QTEST_GUILESS_MAIN(TestFragments)
#include "test_fragments.moc"

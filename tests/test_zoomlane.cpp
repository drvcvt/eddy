#include <QtTest>
#include "zoomlane.h"

using namespace eddy;

static ZoomSegment zoom(quint32 id, qint64 start, qint64 end) {
    ZoomSegment z;
    z.id = id;
    z.startMs = start;
    z.endMs = end;
    return z;
}

class TestZoomLane : public QObject {
    Q_OBJECT
private slots:
    void newZoomsTakeTwoSecondsInTheFreeGap() {
        const QVector<ZoomSegment> none;
        QCOMPARE(zoomlane::placeNew(none, 3000, 10000), (std::pair<qint64, qint64>{3000, 5000}));
        // Near the end the zoom moves back so it keeps its two seconds.
        QCOMPARE(zoomlane::placeNew(none, 9000, 10000), (std::pair<qint64, qint64>{8000, 10000}));
        const QVector<ZoomSegment> two{zoom(1, 1000, 3000), zoom(2, 3800, 6000)};
        // An 0.8 s gap takes an 0.8 s zoom.
        QCOMPARE(zoomlane::placeNew(two, 3100, 10000), (std::pair<qint64, qint64>{3000, 3800}));
        QVERIFY(!zoomlane::placeNew(two, 2000, 10000));        // inside a zoom
        const QVector<ZoomSegment> tight{zoom(1, 1000, 3000), zoom(2, 3400, 6000)};
        QVERIFY(!zoomlane::placeNew(tight, 3100, 10000));      // 0.4 s is too short
        QCOMPARE(zoomlane::placeNew(two, 0, 700), (std::pair<qint64, qint64>{0, 700}));
        QVERIFY(!zoomlane::placeNew(none, 0, 400));             // the whole clip is too short
    }

    void insertKeepsOrderAndIdsAreFresh() {
        QVector<ZoomSegment> zooms{zoom(4, 1000, 2000), zoom(9, 5000, 6000)};
        QCOMPARE(zoomlane::nextId(zooms), 10u);
        QCOMPARE(zoomlane::nextId({}), 1u);
        QCOMPARE(zoomlane::insert(zooms, zoom(10, 3000, 4000)), 1);
        QCOMPARE(zooms[1].id, 10u);
        QCOMPARE(zoomlane::insert(zooms, zoom(11, 0, 500)), 0);
        QCOMPARE(zoomlane::indexOf(zooms, 9), 3);
        QCOMPARE(zoomlane::indexOf(zooms, 99), -1);
    }

    void movingStopsAtNeighboursAndTheEnds() {
        QVector<ZoomSegment> zooms{zoom(1, 1000, 2000), zoom(2, 4000, 5000), zoom(3, 8000, 9000)};
        zoomlane::move(zooms, 2, 7500, 10000);
        QCOMPARE(zooms[1].startMs, 7000);    // against the next zoom
        QCOMPARE(zooms[1].endMs, 8000);
        zoomlane::move(zooms, 2, 500, 10000);
        QCOMPARE(zooms[1].startMs, 2000);    // against the previous one
        zoomlane::move(zooms, 1, -300, 10000);
        QCOMPARE(zooms[0].startMs, 0);
        zoomlane::move(zooms, 3, 9500, 10000);
        QCOMPARE(zooms[2].endMs, 10000);
    }

    void edgesKeepHalfASecondAndTheirNeighbours() {
        QVector<ZoomSegment> zooms{zoom(1, 1000, 2000), zoom(2, 4000, 5000)};
        zoomlane::resize(zooms, 2, false, 4200, 10000);
        QCOMPARE(zooms[1].endMs, 4500);      // at least 0.5 s
        zoomlane::resize(zooms, 2, true, 1500, 10000);
        QCOMPARE(zooms[1].startMs, 2000);    // not over the previous zoom
        zoomlane::resize(zooms, 1, false, 99999, 10000);
        QCOMPARE(zooms[0].endMs, 2000);      // not over the next one
        zoomlane::resize(zooms, 2, false, 99999, 10000);
        QCOMPARE(zooms[1].endMs, 10000);
    }

    void snapPicksTheNearestAnchorInReach() {
        QCOMPARE(zoomlane::snap(1050, {1000, 1100}, 60), 1000);
        QCOMPARE(zoomlane::snap(1070, {1000, 1100}, 60), 1100);
        QCOMPARE(zoomlane::snap(1500, {1000, 1100}, 60), 1500);
    }
};

QTEST_GUILESS_MAIN(TestZoomLane)
#include "test_zoomlane.moc"

#include <QtTest>
#include "zoomsuggest.h"

using namespace eddy;

// A pointer that keeps moving at 120 Hz, except where it rests.
static CursorTrack moving(qint64 durationMs, const QVector<QPair<qint64, qint64>> &rests, QPointF restAt = {500, 300}) {
    CursorTrack t;
    t.videoSize = QSize(1920, 1080);
    for (qint64 ms = 0; ms <= durationMs; ms += 8) {
        bool resting = false;
        for (const auto &[a, b] : rests) resting = resting || (ms >= a && ms < b);
        const QPointF p = resting ? restAt : QPointF(100 + (ms / 8) % 1500, 200 + (ms / 3) % 600);
        if (!resting || t.samples.isEmpty() || t.samples.last().pos != p) t.samples.append({ms, p, true});
    }
    return t;
}

class TestZoomSuggest : public QObject {
    Q_OBJECT
private slots:
    void aRestBecomesAZoomOnIt() {
        const CursorTrack track = moving(10000, {{3000, 4200}});
        const TimeMap time(10000, 0, 10000, {});
        const auto zooms = suggestZooms(track, time, 10000, {}, 7, ZoomSegment::Motion::Smooth);
        QCOMPARE(zooms.size(), 1);
        QCOMPARE(zooms[0].id, 7u);
        QCOMPARE(zooms[0].startMs, 2600);
        QCOMPARE(zooms[0].endMs, 4600);
        QCOMPARE(zooms[0].scale, 1.8);
        QCOMPARE(zooms[0].point, QPointF(500, 300));
        QCOMPARE(zooms[0].motion, ZoomSegment::Motion::Smooth);
    }
    void restsTooShortTooLongOrTooCloseAreLeftOut() {
        const TimeMap time(20000, 0, 20000, {});
        QVERIFY(suggestZooms(moving(20000, {{3000, 3300}}), time, 20000, {}, 1, {}).isEmpty());
        QVERIFY(suggestZooms(moving(20000, {{3000, 7000}}), time, 20000, {}, 1, {}).isEmpty());
        // The longer of two close rests wins; the other is within 1.8 s of it.
        const auto close = suggestZooms(moving(20000, {{3000, 4000}, {5504, 7504}}), time, 20000, {}, 1, {});
        QCOMPARE(close.size(), 1);
        QCOMPARE(close[0].startMs, 5104);   // samples come every 8 ms
        // Existing zooms keep their room.
        ZoomSegment existing;
        existing.id = 1; existing.startMs = 4000; existing.endMs = 6000;
        QVERIFY(suggestZooms(moving(20000, {{3000, 4000}}), time, 20000, {existing}, 2, {}).isEmpty());
    }
    void restsInACutAreLeftOut() {
        const CursorTrack track = moving(10000, {{3000, 4200}});
        const TimeMap time(10000, 0, 10000, {{0, 1.0, false}, {2500, 1.0, true}, {5000, 1.0, false}});
        QVERIFY(suggestZooms(track, time, 10000, {}, 1, {}).isEmpty());
    }
    void clickGroupsComeFirst() {
        CursorTrack track = moving(12000, {{5200, 6400}}, QPointF(800, 400));
        for (qint64 ms : {5000, 5800, 6200}) track.clicks.append({ms, 1, true});
        track.clicks.append({6250, 1, false});   // releases do not count
        track.clicks.append({9000, 3, true});    // nor other buttons
        const TimeMap time(12000, 0, 12000, {});
        const auto zooms = suggestZooms(track, time, 12000, {}, 1, {});
        QCOMPARE(zooms.size(), 1);               // the rest inside the group adds nothing
        QCOMPARE(zooms[0].startMs, 4400);
        QCOMPARE(zooms[0].endMs, 7400);
        QCOMPARE(zooms[0].scale, 2.0);
        QVERIFY(QRectF(0, 0, 1920, 1080).contains(zooms[0].point));
    }
};

QTEST_GUILESS_MAIN(TestZoomSuggest)
#include "test_zoomsuggest.moc"

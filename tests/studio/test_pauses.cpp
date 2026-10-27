// C2: finding the dead air in a recording from its sound, pointer and picture.
#include <QtTest>
#include <cmath>
#include "studio/zoomsuggest.h"

using namespace eddy;

namespace {

// Sound at -10 dB except where `quiet` says, there a -60 dB room tone.
std::function<float(qint64, qint64)> sound(QVector<TimeSpan> quiet) {
    return [quiet](qint64 from, qint64 to) {
        for (const auto &[a, b] : quiet)
            if (from >= a && to <= b) return float(std::pow(10.0, -60 / 20.0));
        return float(std::pow(10.0, -10 / 20.0));
    };
}

CursorTrack stillPointer(QVector<qint64> movesAt = {}, QVector<qint64> clicksAt = {}) {
    CursorTrack track;
    track.videoSize = QSize(1920, 1080);
    track.samples.append({0, QPointF(500, 500), true});
    qreal x = 500;
    for (const qint64 at : movesAt) track.samples.append({at, QPointF(x += 200, 500), true});
    for (const qint64 at : clicksAt) track.clicks.append({at, 1, true});
    return track;
}

PauseSignals base(qint64 durationMs = 8000) {
    PauseSignals input;
    input.time = TimeMap(durationMs, 0, durationMs, {});
    return input;
}

}

class TestPauses : public QObject {
    Q_OBJECT
private slots:
    void silenceAndAStillPointerMakeAPause() {
        PauseSignals input = base();
        input.peak = sound({{2000, 5000}});
        const CursorTrack track = stillPointer();
        input.pointer = &track;
        QCOMPARE(findPauses(input), (QVector<TimeSpan>{{2200, 4800}}));   // 200 ms kept each side
    }
    void aMoveOrAClickSplitsIt() {
        PauseSignals input = base();
        input.peak = sound({{2000, 5000}});
        const CursorTrack moved = stillPointer({3000});
        input.pointer = &moved;
        // The move at 3 s blocks 2.9..3.1 s: 2..2.9 s is under a second, 3.1..5 s stays.
        QCOMPARE(findPauses(input), (QVector<TimeSpan>{{3300, 4800}}));
        const CursorTrack clicked = stillPointer({}, {3500});
        input.pointer = &clicked;
        QCOMPARE(findPauses(input), (QVector<TimeSpan>{{2200, 3200}, {3800, 4800}}));
    }
    void shortSilenceIsNoPause() {
        PauseSignals input = base();
        input.peak = sound({{2000, 2900}});
        QVERIFY(findPauses(input).isEmpty());
    }
    void theClipsStartKeepsNoBreath() {
        PauseSignals input = base();
        input.peak = sound({{0, 1500}});
        QCOMPARE(findPauses(input), (QVector<TimeSpan>{{0, 1300}}));
    }
    void cutAndSpedPiecesAreLeftAlone() {
        Fragment kept, cut, fast;
        cut.startMs = 2000;
        cut.removed = true;
        kept.startMs = 5000;
        fast.startMs = 6000;
        fast.speed = 2;
        PauseSignals input;
        input.time = TimeMap(9000, 0, 9000, {Fragment(), cut, kept, fast});
        input.peak = sound({{2500, 4500}, {6200, 8800}});
        QVERIFY(findPauses(input).isEmpty());
    }
    void aQuietRoomStillFindsItsSilence() {
        // Speech at -10 dB over a -50 dB floor: the threshold follows the floor, not a fixed level.
        PauseSignals input = base();
        input.peak = [](qint64 from, qint64) {
            const bool talking = from < 2000 || from >= 5000;
            return float(std::pow(10.0, (talking ? -10 : -50) / 20.0));
        };
        QCOMPARE(findPauses(input), (QVector<TimeSpan>{{2200, 4800}}));
    }
    void aFrozenPictureDecidesAlone() {
        PauseSignals input = base();
        input.frozen = QVector<TimeSpan>{{1000, 4000}};
        QCOMPARE(findPauses(input), (QVector<TimeSpan>{{1200, 3800}}));
        PauseSignals nothing = base();
        QVERIFY(findPauses(nothing).isEmpty());   // no signal at all: no guess
    }
};

QTEST_GUILESS_MAIN(TestPauses)
#include "test_pauses.moc"

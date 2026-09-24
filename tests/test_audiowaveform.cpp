#include <QtTest>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "audiowaveform.h"
#include "mediaio.h"

using namespace eddy;

static bool have(const QString &cmd) { return !QStandardPaths::findExecutable(cmd).isEmpty(); }

static bool ffmpeg(const QStringList &args) {
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{"-v", "error", "-y"} + args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static const QString kVideo = QStringLiteral("color=c=black:s=64x48:d=3:r=25");
// Two 10 ms clicks, at 1.0 s and 2.5 s of the sound.
static const QString kClicks = QStringLiteral(
    "aevalsrc=exprs=if(between(t\\,1\\,1.01)+between(t\\,2.5\\,2.51)\\,0.9\\,0):s=48000:d=3");

static bool settle(AudioWaveformProvider &p) {
    QElapsedTimer t;
    t.start();
    while (p.state() == AudioWaveformProvider::State::Loading && t.elapsed() < 20000) QTest::qWait(10);
    return p.state() == AudioWaveformProvider::State::Ready;
}

// The loudest bin between `from` and `to`, in ms.
static qint64 loudest(const AudioWaveformProvider &p, qint64 from, qint64 to) {
    qint64 best = -1;
    float peak = 0;
    for (qint64 ms = from; ms < to; ms += p.binMs())
        if (const auto s = p.summary(ms, ms + p.binMs()); s.peak > peak) { peak = s.peak; best = ms; }
    return best;
}

class TestAudioWaveform : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe"))) QSKIP("ffmpeg/ffprobe not available");
    }
    void clicksLandInTheirBinsAndSilenceIsKnown() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("clicks.mkv"));
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", kVideo, "-f", "lavfi", "-i", kClicks, "-c:a", "pcm_s16le", file}));
        AudioWaveformProvider p(file, 3000, 0);
        QVERIFY(!p.summary(0, 10).known);   // nothing read yet: unknown, not silent
        QVERIFY(settle(p));
        QVERIFY(qAbs(loudest(p, 500, 1500) - 1000) <= 10);
        QVERIFY(qAbs(loudest(p, 2000, 3000) - 2500) <= 10);
        // Mono spreads onto two channels at -3 dB: 0.9 shows as about 0.64.
        QVERIFY(p.summary(1000, 1010).peak > 0.6f);
        const auto quiet = p.summary(1400, 2200);
        QVERIFY(quiet.known && quiet.peak < 0.01f);
        // A wide range reads a coarser level and still holds the click.
        QVERIFY(p.summary(0, 3000).peak > 0.6f);
    }
    void oppositePhasesStillShow() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("phase.mkv"));
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", kVideo, "-f", "lavfi", "-i",
                        "aevalsrc=exprs=0.5*sin(2*PI*440*t)|-0.5*sin(2*PI*440*t):s=48000:d=3",
                        "-c:a", "pcm_s16le", file}));
        AudioWaveformProvider p(file, 3000, 0);
        QVERIFY(settle(p));
        const auto tone = p.summary(500, 1500);
        QVERIFY2(tone.rms > 0.25f && tone.peak > 0.45f, qPrintable(QStringLiteral("%1 %2").arg(tone.rms).arg(tone.peak)));
    }
    void aLateSoundStartsLateOnTheVideoAxis() {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("late.mp4"));
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", kVideo, "-itsoffset", "0.5", "-f", "lavfi", "-i",
                        QString(kClicks).replace(QStringLiteral("d=3"), QStringLiteral("d=2")),
                        "-pix_fmt", "yuv420p", "-c:a", "aac", file}));
        const auto probe = probeVideoFile(file);
        QVERIFY(probe.ok && probe.info.hasAudio);
        QVERIFY2(qAbs(probe.info.audioOffsetMs - 500) <= 30, qPrintable(QString::number(probe.info.audioOffsetMs)));
        AudioWaveformProvider p(file, probe.info.durationMs, probe.info.audioOffsetMs);
        QVERIFY(settle(p));
        const qint64 click = loudest(p, 1000, 2000);
        QVERIFY2(qAbs(click - 1500) <= 30, qPrintable(QString::number(click)));
        QVERIFY(p.summary(0, 400).known);   // before the sound: silence, not unknown
    }
    void cancellingStopsAndFileWithoutSoundFails() {
        QTemporaryDir dir;
        const QString clicks = dir.filePath(QStringLiteral("clicks.mkv"));
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", kVideo, "-f", "lavfi", "-i", kClicks, "-c:a", "pcm_s16le", clicks}));
        AudioWaveformProvider cancelled(clicks, 3000, 0);
        QSignalSpy changes(&cancelled, &AudioWaveformProvider::changed);
        cancelled.cancel();
        QCOMPARE(cancelled.state(), AudioWaveformProvider::State::Failed);
        QTest::qWait(300);
        QCOMPARE(changes.count(), 0);
        const QString silent = dir.filePath(QStringLiteral("silent.mp4"));
        QVERIFY(ffmpeg({"-f", "lavfi", "-i", kVideo, "-pix_fmt", "yuv420p", silent}));
        AudioWaveformProvider none(silent, 3000, 0);
        QVERIFY(!settle(none));
        QCOMPARE(none.state(), AudioWaveformProvider::State::Failed);
    }
};

QTEST_GUILESS_MAIN(TestAudioWaveform)
#include "test_audiowaveform.moc"

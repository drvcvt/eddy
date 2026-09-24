#include <QtTest>
#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "mediaio.h"

using namespace eddy;

static QByteArray pngBytes(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray b;
    QBuffer buf(&b);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return b;
}

static bool have(const QString &cmd) {
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

static bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

class TestMediaIo : public QObject {
    Q_OBJECT
private slots:
    void parsesPreciseTimesWithoutOverflow() {
        qint64 ms = -1;
        QVERIFY(parseVideoTime("1:02.5", &ms)); QCOMPARE(ms, 62500);
        QVERIFY(parseVideoTime("1:02:03.004", &ms)); QCOMPARE(ms, 3723004);
        QVERIFY(parseVideoTime("75", &ms)); QCOMPARE(ms, 75000);
        for (const QString &bad : {"-1", "1:60", "1.2345", "1:2:3:4", "nan", "",
                                  "99999999999999999999999999", "9223372036854775807"})
            QVERIFY2(!parseVideoTime(bad, &ms), qPrintable(bad));
    }
    void recognizesCommonVideoExtensions() {
        QVERIFY(pathLooksLikeVideo(QStringLiteral("clip.mp4")));
        QVERIFY(pathLooksLikeVideo(QStringLiteral("clip.webm")));
        QVERIFY(pathLooksLikeVideo(QStringLiteral("clip.mov")));
        QVERIFY(!pathLooksLikeVideo(QStringLiteral("shot.png")));
        QCOMPARE(mediaMimeTypeForPath(QStringLiteral("clip.mp4")), QStringLiteral("video/mp4"));
        QCOMPARE(mediaMimeTypeForPath(QStringLiteral("clip.webm")), QStringLiteral("video/webm"));
    }

    void loadsImageInputAsImageDocument() {
        QTemporaryFile f("XXXXXX.png");
        QVERIFY(f.open());
        const QByteArray bytes = pngBytes(20, 10);
        QCOMPARE(f.write(bytes), bytes.size());
        f.flush();

        InputSpec spec;
        spec.kind = InputSpec::File;
        spec.path = f.fileName();
        auto r = loadMediaInput(spec);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.document.kind, MediaKind::Image);
        QCOMPARE(r.document.nativeSize(), QSize(20, 10));
    }

    void probesGeneratedVideoInput() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("in.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=1:r=5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            video
        }));

        InputSpec spec;
        spec.kind = InputSpec::File;
        spec.path = video;
        auto r = loadMediaInput(spec);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.document.kind, MediaKind::Video);
        QCOMPARE(r.document.nativeSize(), QSize(64, 48));
        QVERIFY(r.document.video.durationMs > 0);
    }

    void probeKnowsWhetherThereIsSound() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe"))) QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        const QString silent = dir.filePath(QStringLiteral("silent.mp4"));
        const QString loud = dir.filePath(QStringLiteral("loud.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=1:r=25", "-pix_fmt", "yuv420p", silent}));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=1:r=25", "-f", "lavfi", "-i", "sine=duration=1", "-shortest",
            "-pix_fmt", "yuv420p", loud}));
        const auto withoutSound = probeVideoFile(silent);
        const auto withSound = probeVideoFile(loud);
        QVERIFY(withoutSound.ok && withSound.ok);
        QVERIFY(!withoutSound.info.hasAudio);
        QVERIFY(withSound.info.hasAudio);
        QCOMPARE(withSound.info.size, QSize(64, 48));
        QVERIFY(qAbs(withSound.info.audioOffsetMs) <= 30);
    }
    void formatsTimesLikeThePlaybackBar() {
        QCOMPARE(formatTime(1999), QStringLiteral("0:01"));
        QCOMPARE(formatTime(3661900), QStringLiteral("1:01:01"));
        QCOMPARE(formatPreciseTime(2500), QStringLiteral("0:02.500"));
    }
    void generatesContactSheetInMemory() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("in.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("testsrc2=s=96x54:d=2:r=8"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video
        }));

        const ContactSheetResult result = generateVideoContactSheet(video, 2000, 4, QSize(80, 45));
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.image.size(), QSize(320, 45));
    }
};

QTEST_GUILESS_MAIN(TestMediaIo)
#include "test_mediaio.moc"

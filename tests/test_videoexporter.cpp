#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "mediaio.h"
#include "videoexporter.h"

using namespace eddy;

static bool have(const QString &cmd) {
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

static bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(20000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static QByteArray processOutput(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return p.readAllStandardOutput().trimmed();
}

class TestVideoExporter : public QObject {
    Q_OBJECT
private slots:
    void rejectsStdoutOutput() {
        QImage overlay(16, 16, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        auto r = writeVideoWithOverlay({
            QStringLiteral("/tmp/in.mp4"),
            QStringLiteral("-"),
            overlay
        });
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("stdout")));
    }

    void exportsStaticOverlayOntoVideo() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("output.mp4"));
        const QString frame = dir.filePath(QStringLiteral("frame.png"));

        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=1:r=5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            input
        }));

        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        {
            QPainter p(&overlay);
            p.fillRect(QRect(0, 0, 24, 24), Qt::red);
        }

        auto r = writeVideoWithOverlay({input, output, overlay});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QFileInfo::exists(output));

        auto probe = probeVideoFile(output);
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QCOMPARE(probe.info.size, QSize(64, 48));

        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"),
            QStringLiteral("-i"), output,
            QStringLiteral("-frames:v"), QStringLiteral("1"),
            frame
        }));
        QImage extracted(frame);
        QVERIFY(!extracted.isNull());
        const QColor c = extracted.pixelColor(8, 8);
        QVERIFY2(c.red() > 120 && c.green() < 100 && c.blue() < 100,
                 qPrintable(QStringLiteral("expected red-ish overlay pixel, got %1,%2,%3")
                                .arg(c.red()).arg(c.green()).arg(c.blue())));
    }

    void trimsVideoAndKeepsAudioInSync() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("output.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=2:r=25"),
            QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=2"),
            QStringLiteral("-shortest"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            input
        }));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);

        auto result = writeVideoWithOverlay({input, output, overlay, 500, 1500});
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(output);
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QVERIFY2(qAbs(probe.info.durationMs - 1000) <= 80,
                 qPrintable(QStringLiteral("duration was %1 ms").arg(probe.info.durationMs)));
        const QByteArray audio = processOutput(QStringLiteral("ffprobe"), {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-select_streams"), QStringLiteral("a:0"),
            QStringLiteral("-show_entries"), QStringLiteral("stream=index"),
            QStringLiteral("-of"), QStringLiteral("csv=p=0"), output
        });
        QVERIFY2(!audio.isEmpty(), "trimmed output lost its audio stream");
    }
};

QTEST_GUILESS_MAIN(TestVideoExporter)
#include "test_videoexporter.moc"

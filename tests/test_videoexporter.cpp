#include <QtTest>
#include <QImage>
#include <QElapsedTimer>
#include <QFile>
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

static double horizontalContrast(const QImage &image, const QRect &rect) {
    qint64 total = 0;
    qint64 samples = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x < rect.right(); ++x) {
            total += qAbs(qGray(image.pixel(x, y)) - qGray(image.pixel(x + 1, y)));
            ++samples;
        }
    }
    return samples > 0 ? double(total) / double(samples) : 0.0;
}

class TestVideoExporter : public QObject {
    Q_OBJECT
private slots:
    void replacesExistingFileAtomically() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("neu-ü.mp4"));
        const QString destination = dir.filePath(QStringLiteral("alt-ä.mp4"));
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        QCOMPARE(sourceFile.write("new"), qint64(3));
        sourceFile.close();
        QFile destinationFile(destination);
        QVERIFY(destinationFile.open(QIODevice::WriteOnly));
        QCOMPARE(destinationFile.write("old"), qint64(3));
        destinationFile.close();

        const auto result = replaceFileAtomically(source, destination);

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(source));
        QFile replaced(destination);
        QVERIFY(replaced.open(QIODevice::ReadOnly));
        QCOMPARE(replaced.readAll(), QByteArray("new"));
    }

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

    void timesOutHungFfmpeg() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake ffmpeg helper is not available on Windows");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString fakeFfmpeg = dir.filePath(QStringLiteral("ffmpeg"));
        QFile script(fakeFfmpeg);
        QVERIFY(script.open(QIODevice::WriteOnly));
        const QByteArray scriptBody("#!/bin/sh\nwhile :; do :; done\n");
        QCOMPARE(script.write(scriptBody), qint64(scriptBody.size()));
        script.close();
        QVERIFY(QFile::setPermissions(fakeFfmpeg,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);
        QImage overlay(16, 16, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QElapsedTimer timer;
        timer.start();
        const auto result = writeVideoWithOverlay({
            QStringLiteral("/tmp/in.mp4"), dir.filePath(QStringLiteral("out.mp4")),
            overlay, 0, -1, 100
        });
        qputenv("PATH", oldPath);

        QVERIFY(!result.ok);
        QVERIFY2(timer.elapsed() < 1000, "hung ffmpeg was not terminated promptly");
        QVERIFY(result.error.contains(QStringLiteral("timed out")));
#endif
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

    void exportsBlurredRegionsOntoVideo() {
        if (!have(QStringLiteral("ffmpeg")))
            QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("output.mp4"));
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"),
            QStringLiteral("nullsrc=s=96x64:d=1:r=5,geq=lum='mod(floor(X/2)+floor(Y/2)+N,2)*219+16':cb=128:cr=128"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), input
        }));

        QImage overlay(96, 64, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, output, overlay};
        request.blurRects = {QRect(20, 12, 56, 40)};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-i"), output,
            QStringLiteral("-frames:v"), QStringLiteral("1"), frame
        }));

        const QImage extracted(frame);
        QVERIFY(!extracted.isNull());
        const double outside = horizontalContrast(extracted, QRect(2, 12, 14, 40));
        const double inside = horizontalContrast(extracted, QRect(28, 20, 40, 24));
        QVERIFY2(outside > 60.0,
                 qPrintable(QStringLiteral("outside contrast was only %1").arg(outside)));
        QVERIFY2(inside < outside * 0.35,
                 qPrintable(QStringLiteral("inside contrast %1 was not below outside %2")
                                .arg(inside).arg(outside)));
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

    void capsHighFrameRateForCompatibleTrimmedOutput() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("output.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=1502x946:d=0.5:r=240"),
            QStringLiteral("-preset"), QStringLiteral("ultrafast"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), input
        }));
        QImage overlay(1502, 946, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);

        const auto result = writeVideoWithOverlay({input, output, overlay, 50, 350});
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(output);
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QVERIFY2(probe.info.fps <= 60.1,
                 qPrintable(QStringLiteral("trimmed output kept %1 fps").arg(probe.info.fps)));
        const int level = processOutput(QStringLiteral("ffprobe"), {
            QStringLiteral("-v"), QStringLiteral("error"),
            QStringLiteral("-select_streams"), QStringLiteral("v:0"),
            QStringLiteral("-show_entries"), QStringLiteral("stream=level"),
            QStringLiteral("-of"), QStringLiteral("csv=p=0"), output
        }).toInt();
        QVERIFY2(level > 0 && level <= 42,
                 qPrintable(QStringLiteral("trimmed output uses H.264 level %1").arg(level)));
        QVERIFY2(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-xerror"), QStringLiteral("-i"), output,
            QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")
        }), "trimmed output cannot be decoded completely");
    }
};

QTEST_GUILESS_MAIN(TestVideoExporter)
#include "test_videoexporter.moc"

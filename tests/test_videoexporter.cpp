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
#include "studiostyle.h"

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

// Binary output as it came, for raw audio.
static QByteArray rawOutput(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    return p.readAllStandardOutput();
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

    void replacesMissingDestination() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("neu-ü.mp4"));
        const QString destination = dir.filePath(QStringLiteral("fehlt-ö.mp4"));
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::WriteOnly));
        QCOMPARE(sourceFile.write("new"), qint64(3));
        sourceFile.close();

        const auto result = replaceFileAtomically(source, destination);

        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(source));
        QFile moved(destination);
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("new"));
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

    void rejectsInvalidCropBeforeStartingEncoder() {
        QImage overlay(160, 120, QImage::Format_ARGB32_Premultiplied); overlay.fill(Qt::transparent);
        for (QRect crop : {QRect(1, 0, 80, 60), QRect(0, 0, 81, 60), QRect(100, 0, 80, 60), QRect(2, 2, -4, 8), QRect(2, 2, 0, 0)}) {
            VideoExportRequest request{"missing.mp4", "output.mp4", overlay}; request.cropRect = crop;
            const auto result = writeVideoWithOverlay(request);
            QVERIFY(!result.ok); QVERIFY(result.error.contains("crop"));
        }
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

    void stopsFfmpegThatMakesNoProgress() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake ffmpeg helper is not available on Windows");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile script(dir.filePath(QStringLiteral("ffmpeg")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nwhile :; do echo out_time_us=0; echo progress=continue; sleep 0.05; done\n");
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);
        QImage overlay(16, 16, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{QStringLiteral("/tmp/in.mp4"), dir.filePath(QStringLiteral("out.mp4")),
                                   overlay, 0, -1, 4000};
        request.stallTimeoutMs = 300;
        QElapsedTimer timer;
        timer.start();
        const auto result = writeVideoWithOverlay(request);
        qputenv("PATH", oldPath);

        QVERIFY(!result.ok);
        QVERIFY2(timer.elapsed() < 2000, "stalled ffmpeg ran into the overall timeout");
        QVERIFY2(result.error.contains(QStringLiteral("progress")), qPrintable(result.error));
#endif
    }

    void cancelStopsRunningFfmpeg() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake ffmpeg helper is not available on Windows");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile script(dir.filePath(QStringLiteral("ffmpeg")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\nwhile :; do sleep 0.05; done\n");
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);
        QImage overlay(16, 16, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{QStringLiteral("/tmp/in.mp4"), dir.filePath(QStringLiteral("out.mp4")),
                                   overlay, 0, -1, 4000};
        QElapsedTimer timer;
        timer.start();
        request.cancelled = [&timer] { return timer.elapsed() > 200; };
        const auto result = writeVideoWithOverlay(request);
        qputenv("PATH", oldPath);

        QVERIFY(!result.ok);
        QVERIFY2(timer.elapsed() < 1500, "cancelled export kept running");
        QVERIFY2(result.error.contains(QStringLiteral("cancelled")), qPrintable(result.error));
#endif
    }

    void framesVideoWithStudioBackground() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=red:s=320x240:d=0.6:r=10", "-pix_fmt", "yuv420p", input}));
        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.cropRect = QRect(0, 0, 200, 100);
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = QColor(0, 0, 255);
        request.studio.padding = 21;   // an odd 21 px offset: yuv420 pad would round it
        request.studio.radius = 20;
        request.studio.shadow = 0;
        const auto result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY(probe.ok);
        QCOMPARE(probe.info.size, QSize(242, 142));   // 200x100 + 2 * 21% of 100
        QCOMPARE(probe.info.durationMs / 100, qint64(6));
        QCOMPARE(qRound(probe.info.fps), 10);   // the video, not the still, sets the rate
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QImage decoded(frame);
        const auto near = [](QColor a, QColor b) {
            return qAbs(a.red() - b.red()) < 40 && qAbs(a.green() - b.green()) < 40
                && qAbs(a.blue() - b.blue()) < 40;
        };
        QVERIFY2(near(decoded.pixelColor(5, 5), Qt::blue), "background");
        QVERIFY2(near(decoded.pixelColor(120, 70), Qt::red), "content");
        // Radius 20% of 100 px: the content's own corner shows the background.
        QVERIFY2(near(decoded.pixelColor(22, 22), Qt::blue), "rounded corner");
        // Video and frame hole must line up to the pixel on every edge: no
        // column of pad colour and no background eating into the content.
        const QRect content(20, 20, 200, 100);   // origin snapped to even
        for (const QPoint &p : {QPoint(content.left(), 70), QPoint(content.right(), 70),
                                QPoint(120, content.top()), QPoint(120, content.bottom())}) {
            const QColor c = decoded.pixelColor(p);
            QVERIFY2(c.red() > 150 && c.blue() < 110,
                     qPrintable(QStringLiteral("edge %1,%2 is %3").arg(p.x()).arg(p.y()).arg(c.name())));
        }
    }

    void framesAnnotatedVideo() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=red:s=200x100:d=0.3:r=10", "-pix_fmt", "yuv420p", input}));
        QImage overlay(200, 100, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter(&overlay).fillRect(QRect(80, 30, 40, 40), Qt::green);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = QColor(0, 0, 255);
        request.studio.padding = 20;
        request.studio.radius = 0;
        request.studio.shadow = 0;
        const auto result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QImage decoded(frame);
        QCOMPARE(decoded.size(), QSize(240, 140));
        QVERIFY(decoded.pixelColor(5, 5).blue() > 200);
        const QColor mark = decoded.pixelColor(20 + 100, 20 + 50);   // annotation, shifted by the padding
        QVERIFY2(mark.green() > 200 && mark.red() < 60, qPrintable(mark.name()));
        QVERIFY(decoded.pixelColor(20 + 20, 20 + 50).red() > 200);
    }

    void reportsEncodingProgress() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "testsrc=s=320x240:d=3:r=30", "-pix_fmt", "yuv420p", input}));
        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        QList<int> reported;
        request.progress = [&reported](int percent) { reported.append(percent); };
        const auto result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!reported.isEmpty());
        QVERIFY2(std::is_sorted(reported.cbegin(), reported.cend()), "progress went backwards");
        QVERIFY(reported.first() >= 0 && reported.last() <= 99);
        QVERIFY2(reported.last() >= 50, qPrintable(QStringLiteral("final progress %1").arg(reported.last())));
    }

    void hardwareFailureFallsBackWithoutAnEmptyOverlay() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake ffmpeg helper is not available on Windows");
#else
        const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        if (ffmpeg.isEmpty()) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath("input.mp4");
        const QString output = dir.filePath("output.mp4");
        QVERIFY(runProcess(ffmpeg, {"-v", "error", "-f", "lavfi", "-i",
            "color=c=red:s=64x48:d=1:r=10", "-pix_fmt", "yuv420p", input}));
        const auto quoted = [](QString value) { return "'" + value.replace("'", "'\\''") + "'"; };
        QFile script(dir.filePath("ffmpeg"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(("#!/bin/sh\ncase \" $* \" in\n"
            " *' color=size=320x180:rate=30 '*) exit 0;;\n"
            " *' h264_nvenc '*) printf rejected > " + quoted(dir.filePath("rejected")) + "; exit 1;;\n"
            " *' -loop '*) exit 2;;\nesac\nexec " + quoted(ffmpeg) + " \"$@\"\n").toUtf8());
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);
        QImage overlay(64, 48, QImage::Format_ARGB32); overlay.fill(Qt::transparent);
        VideoExportRequest request{input, output, overlay, 200, 800};
        request.cropRect = QRect(8, 4, 32, 24);
        const auto result = writeVideoWithOverlay(request);
        qputenv("PATH", oldPath);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(QFileInfo::exists(dir.filePath("rejected")));
        const QString frame = dir.filePath("frame.png");
        QVERIFY(runProcess(ffmpeg, {"-v", "error", "-i", output, "-frames:v", "1", frame}));
        const QImage decoded(frame);
        QCOMPARE(decoded.size(), QSize(32, 24));
        QVERIFY(decoded.pixelColor(16, 12).red() > 230);
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

    void cropsAfterAnnotations() {
        if (!have("ffmpeg")) QSKIP("ffmpeg unavailable");
        QTemporaryDir dir;
        const auto input = dir.filePath("input.mp4");
        const auto output = dir.filePath("output.mp4");
        const auto frame = dir.filePath("frame.png");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=0.2:r=5", "-pix_fmt", "yuv420p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied); overlay.fill(Qt::transparent);
        { QPainter painter(&overlay); painter.fillRect(24, 16, 24, 24, Qt::red); }
        VideoExportRequest request{input, output, overlay}; request.cropRect = {20, 12, 40, 32};
        const auto result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-y", "-i", output, "-frames:v", "1", frame}));
        const QImage decoded(frame);
        QCOMPARE(decoded.size(), QSize(40, 32));
        QVERIFY(decoded.pixelColor(12, 12).red() > 200);
        QVERIFY(decoded.pixelColor(34, 12).red() < 30);
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
    void renderedExportMatchesTheFilterGraph() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        // Red left half, blue right half: the blur below straddles the edge.
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=red:s=160x240:d=1:r=10", "-f", "lavfi", "-i", "color=c=blue:s=160x240:d=1:r=10",
            "-filter_complex", "[0:v][1:v]hstack", "-pix_fmt", "yuv420p", input}));
        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter(&overlay).fillRect(QRect(40, 40, 40, 40), Qt::green);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("graph.mp4")), overlay};
        request.cropRect = QRect(20, 20, 280, 200);
        request.blurRects = {QRect(140, 100, 40, 40)};
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = QColor(255, 255, 0);
        request.studio.padding = 10;
        request.studio.radius = 10;
        request.studio.shadow = 0;
        const DeliverResult graph = writeVideoWithOverlay(request);
        QVERIFY2(graph.ok, qPrintable(graph.error));
        const QString graphPath = request.outputPath;
        request.outputPath = dir.filePath(QStringLiteral("rendered.mp4"));
        const DeliverResult rendered = writeVideoRendered(request);
        QVERIFY2(rendered.ok, qPrintable(rendered.error));

        const auto a = probeVideoFile(graphPath), b = probeVideoFile(request.outputPath);
        QVERIFY(a.ok && b.ok);
        QCOMPARE(b.info.size, a.info.size);
        QVERIFY2(qAbs(a.info.durationMs - b.info.durationMs) <= 100,
                 qPrintable(QStringLiteral("%1 vs %2 ms").arg(a.info.durationMs).arg(b.info.durationMs)));
        QCOMPARE(qRound(b.info.fps), 60);
        auto frameAt = [&](const QString &video, const QString &name) {
            const QString png = dir.filePath(name);
            return runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", video,
                                                         "-frames:v", "1", png}) ? QImage(png) : QImage();
        };
        const QImage fromGraph = frameAt(graphPath, QStringLiteral("graph.png"));
        const QImage fromRender = frameAt(request.outputPath, QStringLiteral("rendered.png"));
        QVERIFY(!fromGraph.isNull() && fromGraph.size() == fromRender.size());
        const QPoint origin = studioLayout(QSize(280, 200), request.studio).content.topLeft();
        // Background, red, blue, the annotation and the middle of the blur.
        for (const QPoint &p : {QPoint(4, 4), origin + QPoint(60, 150), origin + QPoint(220, 150),
                                origin + QPoint(40, 40), origin + QPoint(140, 100)}) {
            const QColor g = fromGraph.pixelColor(p), r = fromRender.pixelColor(p);
            QVERIFY2(qAbs(g.red() - r.red()) <= 10 && qAbs(g.green() - r.green()) <= 10
                         && qAbs(g.blue() - r.blue()) <= 10,
                     qPrintable(QStringLiteral("%1,%2: %3 vs %4").arg(p.x()).arg(p.y())
                                    .arg(g.name(), r.name())));
        }
        qint64 difference = 0;
        for (int y = 0; y < fromGraph.height(); ++y)
            for (int x = 0; x < fromGraph.width(); ++x) {
                const QRgb g = fromGraph.pixel(x, y), r = fromRender.pixel(x, y);
                difference += qAbs(qRed(g) - qRed(r)) + qAbs(qGreen(g) - qGreen(r)) + qAbs(qBlue(g) - qBlue(r));
            }
        const double mean = double(difference) / (3.0 * fromGraph.width() * fromGraph.height());
        QVERIFY2(mean < 3.0, qPrintable(QStringLiteral("mean difference %1").arg(mean)));
    }

    void zoomSegmentFillsTheOutputWithItsTarget() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage quadrants(320, 240, QImage::Format_RGB32);
        {
            QPainter p(&quadrants);
            p.fillRect(0, 0, 160, 120, Qt::red);
            p.fillRect(160, 0, 160, 120, Qt::green);
            p.fillRect(0, 120, 160, 120, Qt::blue);
            p.fillRect(160, 120, 160, 120, Qt::white);
        }
        const QString still = dir.filePath(QStringLiteral("quadrants.png"));
        QVERIFY(quadrants.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(240, 60),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY(probe.ok);
        QCOMPARE(probe.info.size, QSize(320, 240));
        QCOMPARE(qRound(probe.info.fps), 60);   // zooms went through the frame renderer
        // Qt draws RGB; the output says which matrix turned it into YUV, so players do not guess.
        QCOMPARE(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=color_space", "-of", "csv=p=0", request.outputPath}),
                 QByteArray("bt709"));
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QImage decoded(frame);
        for (const QPoint &p : {QPoint(20, 20), QPoint(300, 20), QPoint(20, 220), QPoint(300, 220), QPoint(160, 120)}) {
            const QColor c = decoded.pixelColor(p);
            QVERIFY2(c.green() > 200 && c.red() < 60 && c.blue() < 60,
                     qPrintable(QStringLiteral("%1,%2 is %3").arg(p.x()).arg(p.y()).arg(c.name())));
        }
    }

    void keepZoomedInCropsToTheFramesRatio() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = Qt::green;
        request.studio.padding = 0;
        request.studio.radius = 0;
        request.studio.shadow = 0;
        request.studio.aspect = QSize(9, 16);
        request.baseView = keepZoomedInRect(QRect(0, 0, 320, 180), request.studio, QPointF(240, 90));
        QCOMPARE(request.baseView, QRect(190, 0, 100, 180));
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY(probe.ok);
        QCOMPARE(probe.info.size, QSize(102, 180));   // 9:16 with a 2 px background grow
        QCOMPARE(qRound(probe.info.fps), 30);         // a static crop stays on the filter graph
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QColor c = QImage(frame).pixelColor(51, 90);
        QVERIFY2(c.blue() > 200 && c.red() < 60, qPrintable(c.name()));

        // With a zoom the renderer takes over and zooms inside the same window.
        request.outputPath = dir.filePath(QStringLiteral("zoom.mp4"));
        request.baseView = QRect(110, 0, 100, 180);    // straddles both halves
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(135, 90),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult zoomed = writeVideoWithOverlay(request);
        QVERIFY2(zoomed.ok, qPrintable(zoomed.error));
        const auto zoomProbe = probeVideoFile(request.outputPath);
        QCOMPARE(zoomProbe.info.size, QSize(102, 180));
        QCOMPARE(qRound(zoomProbe.info.fps), 60);
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        // 2x around x = 135 shows 110..160 (red) across the whole width.
        const QImage decoded(frame);
        for (int x : {8, 51, 94}) {
            const QColor p = decoded.pixelColor(x, 90);
            QVERIFY2(p.red() > 200 && p.blue() < 60, qPrintable(QStringLiteral("%1: %2").arg(x).arg(p.name())));
        }
    }
    void rejectsABaseViewOutsideTheCrop() {
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        VideoExportRequest request{QStringLiteral("in.mp4"), QStringLiteral("out.mp4"), overlay};
        request.cropRect = QRect(0, 0, 160, 180);
        request.baseView = QRect(100, 0, 100, 180);
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("view")), qPrintable(result.error));
    }
    void shrinksAndCapsFramesForSmallerExports() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "testsrc2=s=640x360:r=60:d=1", "-f", "lavfi", "-i", "sine=frequency=440:duration=1",
            "-shortest", "-pix_fmt", "yuv420p", input}));
        QImage overlay(640, 360, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("small.mp4")), overlay};
        request.maxShortSide = 180;
        request.maxFps = 30;
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QCOMPARE(probe.info.size, QSize(320, 180));
        QCOMPARE(qRound(probe.info.fps), 30);
        // The same through the frame renderer.
        request.outputPath = dir.filePath(QStringLiteral("small-zoom.mp4"));
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(320, 180),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult rendered = writeVideoWithOverlay(request);
        QVERIFY2(rendered.ok, qPrintable(rendered.error));
        const auto renderedProbe = probeVideoFile(request.outputPath);
        QCOMPARE(renderedProbe.info.size, QSize(320, 180));
        QCOMPARE(qRound(renderedProbe.info.fps), 30);
    }
    void exportsAPalettedGifWithoutSound() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still,
            "-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-t", "1", "-r", "30",
            "-vf", "setsar=1,format=yuv420p", "-shortest", input}));
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        for (bool zoom : {false, true}) {
            VideoExportRequest request{input, dir.filePath(zoom ? QStringLiteral("z.gif") : QStringLiteral("a.gif")), overlay};
            request.maxShortSide = 90;
            request.maxFps = 15;
            if (zoom)
                request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(80, 90),
                                  ZoomSegment::Motion::Instant}};
            const DeliverResult result = writeVideoWithOverlay(request);
            QVERIFY2(result.ok, qPrintable(result.error));
            QCOMPARE(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "v:0",
                     "-show_entries", "stream=codec_name,width,height,r_frame_rate", "-of", "csv=p=0",
                     request.outputPath}), QByteArray("gif,160,90,15/1"));
            QVERIFY(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "a",
                    "-show_entries", "stream=index", "-of", "csv=p=0", request.outputPath}).isEmpty());
            const QString frame = dir.filePath(QStringLiteral("frame.png"));
            QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", "0.5", "-i",
                                                         request.outputPath, "-frames:v", "1", frame}));
            // Zoomed 2x onto the red half, every sample is red; otherwise the halves stay apart.
            const QImage decoded(frame);
            const QColor left = decoded.pixelColor(20, 45), right = decoded.pixelColor(140, 45);
            QVERIFY2(left.red() > 200 && left.blue() < 60, qPrintable(left.name()));
            if (zoom) QVERIFY2(right.red() > 200 && right.blue() < 60, qPrintable(right.name()));
            else QVERIFY2(right.blue() > 200 && right.red() < 60, qPrintable(right.name()));
        }
    }
    void fragmentsCutAndSpeedUpPictureAndSound() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        // Six seconds, one colour per second, a short beep at the start of every second.
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error",
            "-f", "lavfi", "-i", "color=red:s=64x48:r=30:d=1", "-f", "lavfi", "-i", "color=0x00ff00:s=64x48:r=30:d=1",
            "-f", "lavfi", "-i", "color=blue:s=64x48:r=30:d=1", "-f", "lavfi", "-i", "color=yellow:s=64x48:r=30:d=1",
            "-f", "lavfi", "-i", "color=white:s=64x48:r=30:d=1", "-f", "lavfi", "-i", "color=black:s=64x48:r=30:d=1",
            "-f", "lavfi", "-i", "sine=frequency=1000:duration=6,volume='if(lt(mod(t,1),0.05),1,0)':eval=frame",
            "-filter_complex", "[0:v][1:v][2:v][3:v][4:v][5:v]concat=n=6:v=1:a=0[v]",
            "-map", "[v]", "-map", "6:a", "-pix_fmt", "yuv420p", "-c:a", "aac", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        // Keep 0-2 s, cut 2-3 s (blue), 3-5 s at 2x (yellow, white), keep 5-6 s.
        const QVector<Fragment> fragments{{0, 1.0, false}, {2000, 1.0, true}, {3000, 2.0, false}, {5000, 1.0, false}};
        auto colourAt = [&](const QString &file, double seconds) {
            const QString frame = dir.filePath(QStringLiteral("f.png"));
            runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", QString::number(seconds), "-i", file,
                                                  "-frames:v", "1", frame});
            return QImage(frame).pixelColor(32, 24);
        };
        auto duration = [&](const QString &file) {
            return processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-show_entries", "format=duration",
                                                             "-of", "csv=p=0", file}).toDouble();
        };
        for (bool render : {false, true}) {
            VideoExportRequest request{input, dir.filePath(render ? QStringLiteral("r.mp4") : QStringLiteral("g.mp4")), overlay};
            request.fragments = fragments;
            if (render) request.zooms = {{1, 0, 100, 1.1, ZoomSegment::Target::Point, QPointF(32, 24),
                                          ZoomSegment::Motion::Instant}};
            const DeliverResult result = writeVideoWithOverlay(request);
            QVERIFY2(result.ok, qPrintable(result.error));
            // 2 s + 1 s (2 s at 2x) + 1 s = 4 s.
            QVERIFY2(qAbs(duration(request.outputPath) - 4.0) < 0.1,
                     qPrintable(QStringLiteral("render %1: %2 s").arg(render).arg(duration(request.outputPath))));
            const struct { double at; QColor want; } probes[] = {
                {0.5, Qt::red}, {1.5, Qt::green}, {2.25, Qt::yellow}, {2.75, Qt::white}, {3.5, Qt::black}};
            for (const auto &probe : probes) {
                const QColor c = colourAt(request.outputPath, probe.at);
                QVERIFY2(qAbs(c.red() - probe.want.red()) < 60 && qAbs(c.green() - probe.want.green()) < 60
                             && qAbs(c.blue() - probe.want.blue()) < 60,
                         qPrintable(QStringLiteral("render %1 at %2: %3").arg(render).arg(probe.at).arg(c.name())));
            }
            // Beeps start at 0 and 1 s (kept), 2 s (source 3 s) and 2.5 s (source 4 s, at 2x), 3 s (source 5 s).
            const QByteArray pcm = rawOutput(QStringLiteral("ffmpeg"), {"-v", "error", "-i", request.outputPath,
                "-ac", "1", "-ar", "8000", "-f", "s16le", "-"});
            const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
            QVector<double> onsets;
            int quiet = 400;
            for (qsizetype i = 0; i < pcm.size() / 2; ++i) {
                if (qAbs(samples[i]) > 3000) {
                    if (quiet >= 400) onsets.append(i / 8000.0);
                    quiet = 0;
                } else {
                    ++quiet;
                }
            }
            const QVector<double> want{0, 1, 2, 2.5, 3};
            QVERIFY2(onsets.size() == want.size(), qPrintable(QStringLiteral("render %1: %2 beeps").arg(render).arg(onsets.size())));
            for (int i = 0; i < want.size(); ++i)
                QVERIFY2(qAbs(onsets[i] - want[i]) < 0.06,
                         qPrintable(QStringLiteral("render %1 beep %2 at %3 s").arg(render).arg(i).arg(onsets[i])));
        }
    }
    void refusesARangeThatIsAllCut() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=6:r=25", "-pix_fmt", "yuv420p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay, 2000, 4000};
        request.fragments = {{0, 1.0, false}, {1000, 1.0, true}, {5000, 1.0, false}};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("cut")), qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(request.outputPath));
    }
    void renderedExportKeepsTrimAndAudio() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=2:r=25", "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
            "-shortest", "-pix_fmt", "yuv420p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay, 500, 1500};
        request.zooms = {{1, 0, 2000, 1.5, ZoomSegment::Target::Point, QPointF(32, 24),
                          ZoomSegment::Motion::Focused}};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QVERIFY2(qAbs(probe.info.durationMs - 1000) <= 80,
                 qPrintable(QStringLiteral("duration was %1 ms").arg(probe.info.durationMs)));
        const QByteArray audio = processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams",
            "a:0", "-show_entries", "stream=index", "-of", "csv=p=0", request.outputPath});
        QVERIFY2(!audio.isEmpty(), "the rendered export lost its audio stream");
    }
};

QTEST_GUILESS_MAIN(TestVideoExporter)
#include "test_videoexporter.moc"

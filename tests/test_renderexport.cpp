#include <QtTest>
#include <QElapsedTimer>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "renderexport.h"

using namespace eddy;

static QString ffmpeg() { return QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); }

static QByteArray run(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(20000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) return {};
    return p.readAllStandardOutput().trimmed();
}

// One second of a 30 fps test pattern, decoded at 60 fps: 60 frames.
static RenderPipeline pipeline(const QString &output) {
    RenderPipeline job;
    job.ffmpeg = ffmpeg();
    job.sourceSize = QSize(160, 120);
    job.decodeArgs = {"-hide_banner", "-loglevel", "error", "-nostdin", "-f", "lavfi", "-i",
                      "testsrc=s=160x120:d=1:r=30", "-vf", "fps=60,format=bgra",
                      "-f", "rawvideo", "-pix_fmt", "bgra", "-"};
    job.outputSize = QSize(80, 60);
    job.encodeArgs = {"-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "bgra",
                      "-s", "80x60", "-r", "60", "-i", "-", "-c:v", "libx264", "-preset", "veryfast",
                      "-pix_fmt", "yuv420p", output};
    job.expectedFrames = 60;
    // First half second red, then blue: every output frame is ours.
    job.render = [](qint64 index, const QImage &, QImage &out) { out.fill(index < 30 ? Qt::red : Qt::blue); };
    return job;
}

static QColor frameColor(const QString &video, double seconds) {
    const QByteArray png = run(ffmpeg(), {"-v", "error", "-ss", QString::number(seconds), "-i", video,
                                          "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"});
    const QImage image = QImage::fromData(png);
    return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
}

class TestRenderExport : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (ffmpeg().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
            QSKIP("ffmpeg/ffprobe not available");
    }

    void everyFrameIsDrawnByQt() {
        QTemporaryDir dir;
        const QString out = dir.filePath(QStringLiteral("out.mp4"));
        RenderPipeline job = pipeline(out);
        QList<int> reported;
        job.progress = [&](int percent) { reported.append(percent); };
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(run(QStringLiteral("ffprobe"), {"-v", "error", "-count_frames", "-select_streams", "v:0",
                 "-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", out}), QByteArray("60"));
        QVERIFY(frameColor(out, 0.2).red() > 200);
        QVERIFY(frameColor(out, 0.8).blue() > 200);
        QVERIFY(!reported.isEmpty());
        QVERIFY(std::is_sorted(reported.cbegin(), reported.cend()));
        QVERIFY2(reported.last() >= 90 && reported.last() <= 99, qPrintable(QString::number(reported.last())));
    }

    void cancellingStopsBothProcesses() {
        QTemporaryDir dir;
        RenderPipeline job = pipeline(dir.filePath(QStringLiteral("out.mp4")));
        job.decodeArgs[job.decodeArgs.indexOf(QStringLiteral("testsrc=s=160x120:d=1:r=30"))]
            = QStringLiteral("testsrc=s=160x120:d=600:r=30");
        job.render = [](qint64, const QImage &, QImage &out) {
            out.fill(Qt::red);
            QThread::msleep(5);
        };
        QElapsedTimer timer;
        timer.start();
        job.cancelled = [&] { return timer.elapsed() > 300; };
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("cancelled")), qPrintable(result.error));
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
    }

    void anEncoderThatStopsReadingIsStopped() {
        QTemporaryDir dir;
        RenderPipeline job = pipeline(dir.filePath(QStringLiteral("out.mp4")));
        // Never reads stdin, so the frames pile up and nothing is ever written.
        job.encodeArgs = {"-hide_banner", "-loglevel", "error", "-nostdin", "-re", "-f", "lavfi",
                          "-i", "nullsrc=d=600", "-f", "null", "-"};
        job.outputSize = QSize(1280, 720);   // big frames fill the pipe at once
        job.stallTimeoutMs = 500;
        QElapsedTimer timer;
        timer.start();
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("progress")), qPrintable(result.error));
        QVERIFY2(timer.elapsed() < 3000, qPrintable(QString::number(timer.elapsed())));
    }

    void ffmpegErrorsReachTheCaller() {
        QTemporaryDir dir;
        RenderPipeline job = pipeline(dir.filePath(QStringLiteral("out.mp4")));
        job.encodeArgs[job.encodeArgs.indexOf(QStringLiteral("libx264"))] = QStringLiteral("no_such_encoder");
        QElapsedTimer timer;
        timer.start();
        const DeliverResult result = runRenderPipeline(job);
        QVERIFY(!result.ok);
        // ffmpeg's own words: "Unknown encoder …" or "… Encoder not found", by version.
        QVERIFY2(result.error.contains(QStringLiteral("encoder"), Qt::CaseInsensitive), qPrintable(result.error));
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestRenderExport)
#include "test_renderexport.moc"

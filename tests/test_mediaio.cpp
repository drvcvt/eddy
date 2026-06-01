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
};

QTEST_GUILESS_MAIN(TestMediaIo)
#include "test_mediaio.moc"

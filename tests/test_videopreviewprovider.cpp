#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QStandardPaths>
#include "videopreviewprovider.h"
using namespace eddy;

class TestVideoPreviewProvider : public QObject {
    Q_OBJECT
private slots:
    void samplesSourceAndDeliversOnlyLatestHover() {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath("colors.mp4");
        QProcess encoder;
        encoder.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "color=c=red:s=64x48:d=1:r=25", "-f", "lavfi", "-i",
            "color=c=blue:s=64x48:d=1:r=25", "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0",
            "-pix_fmt", "yuv420p", file});
        QVERIFY(encoder.waitForFinished(10000)); QCOMPARE(encoder.exitCode(), 0);
        VideoPreviewProvider provider(file);
        QSignalSpy thumbs(&provider, &VideoPreviewProvider::thumbnailReady);
        QSignalSpy hover(&provider, &VideoPreviewProvider::hoverReady);
        provider.requestStrip({200, 1200}, QSize(64, 48));
        QTRY_COMPARE_WITH_TIMEOUT(thumbs.count(), 2, 5000);
        const QImage red = qvariant_cast<QImage>(thumbs[0][1]);
        const QImage blue = qvariant_cast<QImage>(thumbs[1][1]);
        QVERIFY(red.pixelColor(10, 10).red() > 200);
        QVERIFY(blue.pixelColor(10, 10).blue() > 200);
        provider.requestHover(400, QSize(80, 60));
        provider.requestHover(1400, QSize(80, 60));
        QTRY_COMPARE_WITH_TIMEOUT(hover.count(), 1, 5000);
        QCOMPARE(hover[0][0].toLongLong(), 1400);
        QVERIFY(qvariant_cast<QImage>(hover[0][1]).pixelColor(10, 10).blue() > 200);
        provider.cancelHover();
        provider.requestHover(1400, QSize(80, 60));
        QCOMPARE(hover.count(), 2); // Cached result is immediate, no decoder restart.
    }
    void failedDecoderLeavesHoverUsable() {
        VideoPreviewProvider provider("missing.mp4", nullptr, "/missing/ffmpeg");
        QSignalSpy failed(&provider, &VideoPreviewProvider::hoverFailed);
        provider.requestHover(400, QSize(80, 60));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
        provider.requestHover(800, QSize(80, 60));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 2000);
    }
};
QTEST_MAIN(TestVideoPreviewProvider)
#include "test_videopreviewprovider.moc"

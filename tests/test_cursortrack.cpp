#include <QtTest>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "cursortrack.h"
#include "mediaio.h"

using namespace eddy;

static QByteArray track(const QByteArray &body, int width = 64, int height = 48) {
    return "{\"format\":\"boltsnap.cursor\",\"version\":1,\"width\":" + QByteArray::number(width)
        + ",\"height\":" + QByteArray::number(height) + "," + body + "}";
}

class TestCursorTrack : public QObject {
    Q_OBJECT
private slots:
    void sidecarSitsNextToTheVideo() {
        QCOMPARE(cursorTrackPathFor(QStringLiteral("/tmp/a/clip.v2.mp4")),
                 QStringLiteral("/tmp/a/clip.v2.cursor.json"));
    }

    void parsesSamplesAndClicks() {
        const auto r = parseCursorTrack(track(
            "\"cursor_in_video\":false,"
            "\"samples\":[[0,10,20],[100,20,40],[200,null,null],[300,5.5,6]],"
            "\"clicks\":[[50,1,1],[80,1,0]],\"future_field\":{}"), QSize(64, 48));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.track.cursorInVideo);
        QCOMPARE(r.track.samples.size(), 4);
        QCOMPARE(r.track.clicks.size(), 2);
        QVERIFY(!r.track.clicks.at(1).down);
    }

    void acceptsBoltsnapsFractionalTimesAndImageIds() {
        // The shape boltsnap 1ff3923 writes: fractional ms, an image id per
        // sample, an images map and a render preset, no clicks.
        const auto r = parseCursorTrack(track(
            "\"cursor_in_video\":true,\"clean_video\":\"clip.clean.mp4\","
            "\"samples\":[[0,null,null],[10.6,1.5,2,\"arrow\"],[10.7,2,3,\"arrow\"],[20.125,4,5,\"arrow\"]],"
            "\"images\":{\"arrow\":{\"png\":\"\",\"hotspot\":[1,1],\"scale\":1}},"
            "\"render\":{\"preset\":\"mellow\"}"), QSize(64, 48));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.track.samples.size(), 4);
        QVERIFY(!r.track.samples.first().visible);
        QVERIFY(r.track.clicks.isEmpty());
    }

    void interpolatesVisiblePositions() {
        const auto r = parseCursorTrack(track(
            "\"samples\":[[100,10,20],[200,20,40],[300,null,null],[400,5,6]]"), QSize(64, 48));
        QVERIFY(r.ok);
        const CursorTrack &t = r.track;
        QVERIFY(!t.positionAt(99));
        QCOMPARE(*t.positionAt(100), QPointF(10, 20));
        QCOMPARE(*t.positionAt(150), QPointF(15, 30));
        // No interpolation into a hidden stretch, nothing while hidden.
        QCOMPARE(*t.positionAt(299), QPointF(20, 40));
        QVERIFY(!t.positionAt(350));
        QCOMPARE(*t.positionAt(5000), QPointF(5, 6));
    }

    void rejectsUnusableTracks() {
        const QSize size(64, 48);
        QVERIFY(!parseCursorTrack("nope", size).ok);
        QVERIFY(!parseCursorTrack("{\"format\":\"other\",\"version\":1}", size).ok);
        QVERIFY(!parseCursorTrack(track("\"samples\":[]").replace("\"version\":1", "\"version\":2"), size).ok);
        QVERIFY(parseCursorTrack(track("\"samples\":[]", 32, 24), size).error.contains("32x24"));
        QVERIFY(!parseCursorTrack(track("\"samples\":[[100,1,1],[50,1,1]]"), size).ok);
        // Rounding at a segment seam may step back by a fraction; that is clamped.
        const auto seam = parseCursorTrack(track("\"samples\":[[100.4,1,1],[100,2,2],[101,3,3]]"), size);
        QVERIFY2(seam.ok, qPrintable(seam.error));
        QCOMPARE(seam.track.samples.at(1).ms, qint64(100));
        QVERIFY(!parseCursorTrack(track("\"samples\":[[100,1,1],[98.9,1,1]]"), size).ok);
        QVERIFY(!parseCursorTrack(track("\"samples\":[[-1,1,1]]"), size).ok);
        QVERIFY(!parseCursorTrack(track("\"samples\":[[0,\"x\",1]]"), size).ok);
        QVERIFY(!parseCursorTrack(track("\"clicks\":[[0,7,1]]"), size).ok);
        const auto bad = parseCursorTrack("nope", size);
        QVERIFY(bad.found);
    }

    void videoOpensWithOrWithoutATrack() {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()
            || QStandardPaths::findExecutable("ffprobe").isEmpty()) QSKIP("ffmpeg unavailable");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath("clip.mp4");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=64x48:d=0.2:r=10",
                                "-pix_fmt", "yuv420p", video});
        QVERIFY(ffmpeg.waitForFinished(15000) && ffmpeg.exitCode() == 0);

        auto plain = loadMediaInput({InputSpec::File, video});
        QVERIFY(plain.ok);
        QVERIFY(!plain.document.cursorTrack);
        QVERIFY(plain.warning.isEmpty());

        QFile sidecar(cursorTrackPathFor(video));
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write(track("\"samples\":[[0,1,2]]"));
        sidecar.close();
        auto tracked = loadMediaInput({InputSpec::File, video});
        QVERIFY(tracked.ok);
        QVERIFY(tracked.document.cursorTrack);
        QCOMPARE(tracked.document.cursorTrack->samples.size(), 1);

        QVERIFY(sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate));
        sidecar.write("{broken");
        sidecar.close();
        auto broken = loadMediaInput({InputSpec::File, video});
        QVERIFY2(broken.ok, "a broken cursor track must not block opening the video");
        QVERIFY(!broken.document.cursorTrack);
        QVERIFY(broken.warning.contains("cursor track"));
    }

    void findsOnlyASiblingCleanVideo() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath("clip.mp4");
        QFile clean(dir.filePath("clip.clean.mp4"));
        QVERIFY(clean.open(QIODevice::WriteOnly));
        clean.close();
        const auto withClean = parseCursorTrack(track("\"clean_video\":\"clip.clean.mp4\""),
                                                QSize(64, 48), video);
        QVERIFY(withClean.ok);
        QCOMPARE(withClean.track.cleanVideoPath, QFileInfo(clean).absoluteFilePath());
        for (const QByteArray name : {QByteArray("missing.mp4"), QByteArray("../clip.clean.mp4"),
                                      QByteArray("/etc/passwd"), QByteArray("..")}) {
            const auto r = parseCursorTrack(track("\"clean_video\":\"" + name + "\""),
                                            QSize(64, 48), video);
            QVERIFY2(r.ok, "a bad clean_video must not discard the track");
            QVERIFY2(r.track.cleanVideoPath.isEmpty(), name.constData());
        }
    }
};

QTEST_GUILESS_MAIN(TestCursorTrack)
#include "test_cursortrack.moc"

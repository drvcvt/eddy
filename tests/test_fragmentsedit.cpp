#include <QtTest>
#include <QAudioOutput>
#include <QGraphicsVideoItem>
#include <QLabel>
#include <QMediaPlayer>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QToolButton>
#include <QUndoStack>
#include <QVideoSink>
#include "editorwindow.h"
#include "exportsettings.h"
#include "fragmentbar.h"
#include "videotimeline.h"

using namespace eddy;

static bool have(const QString &cmd) { return !QStandardPaths::findExecutable(cmd).isEmpty(); }

static MediaDocument videoDoc(const QString &path, qint64 duration = 4000) {
    MediaDocument doc;
    doc.kind = MediaKind::Video;
    doc.path = path;
    doc.video = {QSize(160, 90), duration, 30.0};
    return doc;
}

class TestFragmentsEdit : public QObject {
    Q_OBJECT
private slots:
    void splitSpeedCutAndJoinAreUndoSteps() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.resize(900, 640);
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *duration = w.findChild<QLabel *>(QStringLiteral("TrimDuration"));
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_S);
        QCOMPARE(w.studioDocument().fragments.size(), 2);
        QCOMPARE(w.studioDocument().fragments[1].startMs, 1000);
        auto *bar = w.findChild<FragmentBar *>();
        QTRY_VERIFY(bar->isVisible());                     // the part after the split is selected
        const int steps = undo->count();
        w.findChild<QMenu *>(QStringLiteral("FragmentSpeedMenu"))->actions().at(4)->trigger();   // 2x
        QCOMPARE(w.studioDocument().fragments[1].speed, 2.0);
        QCOMPARE(duration->text(), QStringLiteral("0:02.500"));   // 1 s + 3 s at 2x
        w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->click();
        QVERIFY(w.studioDocument().fragments[1].removed);
        QCOMPARE(duration->text(), QStringLiteral("0:01.000"));
        QCOMPARE(w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->text(), QStringLiteral("Restore"));
        w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->click();
        QVERIFY(!w.studioDocument().fragments[1].removed);
        QCOMPARE(undo->count(), steps + 3);
        // Esc lets go of the fragment before it would close the window.
        QTest::keyClick(&w, Qt::Key_Escape);
        QVERIFY(!bar->isVisible());
        QVERIFY(w.isVisible());
        undo->undo();
        undo->undo();
        undo->undo();
        undo->undo();
        QVERIFY(w.studioDocument().fragments.isEmpty());
        QCOMPARE(duration->text(), QStringLiteral("0:04.000"));
    }
    void playbackSkipsACut() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = dir.filePath("colours.mp4");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=red:s=160x90:r=30:d=1",
            "-f", "lavfi", "-i", "color=blue:s=160x90:r=30:d=1", "-f", "lavfi", "-i", "color=0x00ff00:s=160x90:r=30:d=1",
            "-filter_complex", "[0:v][1:v][2:v]concat=n=3:v=1:a=0", "-g", "10", "-pix_fmt", "yuv420p", clip});
        QVERIFY(ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0);
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(clip, 3000), cfg, {});
        w.show();
        QTRY_VERIFY(w.findChild<QMediaPlayer *>());
        auto *player = w.findChild<QMediaPlayer *>();
        player->audioOutput()->setMuted(true);
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        QTRY_VERIFY(player->isSeekable());
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        StudioDocument doc;
        doc.fragments = {{0, 1.0, false}, {1000, 1.0, true}, {2000, 1.0, false}};
        w.setStudioDocument(doc);
        int blue = 0, green = 0;
        connect(video->videoSink(), &QVideoSink::videoFrameChanged, &w, [&](const QVideoFrame &frame) {
            if (player->playbackState() != QMediaPlayer::PlayingState) return;
            const QColor c = frame.toImage().pixelColor(80, 45);
            if (c.blue() > 150 && c.red() < 100 && c.green() < 100) ++blue;
            if (c.green() > 150 && c.red() < 100) ++green;
        });
        player->play();
        QTRY_VERIFY_WITH_TIMEOUT(green > 5, 8000);
        player->pause();
        // A seam may show a frame or two of the cut while the seek lands.
        QVERIFY2(blue <= 2, qPrintable(QStringLiteral("%1 frames from the cut").arg(blue)));
    }
    void playbackStopsBeforeACutAtTheEnd() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = dir.filePath("colours.mp4");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=red:s=160x90:r=30:d=2",
            "-f", "lavfi", "-i", "color=0x00ff00:s=160x90:r=30:d=1",
            "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0", "-g", "10", "-pix_fmt", "yuv420p", clip});
        QVERIFY(ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0);
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(clip, 3000), cfg, {});
        w.show();
        QTRY_VERIFY(w.findChild<QMediaPlayer *>());
        auto *player = w.findChild<QMediaPlayer *>();
        player->audioOutput()->setMuted(true);
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        QTRY_VERIFY(player->isSeekable());
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        StudioDocument doc;
        doc.fragments = {{0, 1.0, false}, {2000, 1.0, true}};
        w.setStudioDocument(doc);
        int green = 0;
        connect(video->videoSink(), &QVideoSink::videoFrameChanged, &w, [&](const QVideoFrame &frame) {
            if (player->playbackState() != QMediaPlayer::PlayingState) return;
            const QColor c = frame.toImage().pixelColor(80, 45);
            if (c.green() > 150 && c.red() < 100) ++green;
        });
        player->play();
        QTRY_VERIFY_WITH_TIMEOUT(player->playbackState() != QMediaPlayer::PlayingState, 6000);
        QVERIFY2(green <= 2, qPrintable(QStringLiteral("%1 frames from the cut").arg(green)));
    }
    void anExplicitOutputKeepsItsOwnFormat() {
        if (!have("ffmpeg") || !have("ffprobe")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=red:s=160x90:r=30:d=1", "-pix_fmt", "yuv420p", clip});
        QVERIFY(ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0);
        CliOptions cli;
        cli.configPath = dir.filePath("config");
        cli.output.toFile = true;
        cli.output.filePath = dir.filePath("out.mp4");
        saveExportSettings(cli.configPath, exportPreset(ExportPreset::Gif));   // chosen some other day
        Config cfg; cfg.animations = false; cfg.copyOnSave = false;
        EditorWindow w(videoDoc(clip, 1000), cfg, cli);
        StudioDocument doc;
        doc.style.background = StudioStyle::Background::Color;
        w.setStudioDocument(doc);
        w.save();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(cli.output.filePath), 20000);
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name",
                                "-of", "csv=p=0", cli.output.filePath});
        QVERIFY(probe.waitForFinished(10000));
        QCOMPARE(probe.readAllStandardOutput().trimmed(), QByteArray("h264"));
    }
};

QTEST_MAIN(TestFragmentsEdit)
#include "test_fragmentsedit.moc"

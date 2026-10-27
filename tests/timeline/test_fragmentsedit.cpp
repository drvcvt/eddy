#include <QtTest>
#include <QAudioOutput>
#include <QGraphicsVideoItem>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QToolButton>
#include <QUndoStack>
#include <QVideoSink>
#include <QWidgetAction>
#include <QContextMenuEvent>
#include "popovers/exportpanel.h"
#include "editor/editorwindow.h"
#include "chrome/toast.h"
#include "app/theme.h"
#include "output/exportsettings.h"
#include "timeline/fragmentbar.h"
#include "timeline/videotimeline.h"
#include <QTimer>
#include "timeline/audiowaveform.h"
#include "bars/redactbar.h"
#include "items/regions/redactitem.h"
#include <QGraphicsScene>

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
    void onlyAVideoWithSoundGetsAWaveform() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow silent(videoDoc(dir.filePath("none.mp4")), cfg, {});
        QVERIFY(!silent.findChild<VideoTimeline *>()->waveformShown());
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow loud(doc, cfg, {});
        QVERIFY(loud.findChild<VideoTimeline *>()->waveformShown());   // its place is kept while it loads
    }
    void leavingTheSoundOutIsAnEditThatUndoes() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow w(doc, cfg, {});
        w.show();
        QAction *include = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Include audio in output")) include = a;
        QVERIFY(include && include->isEnabled() && include->isChecked());
        auto *label = w.findChild<QLabel *>(QStringLiteral("NoAudio"));
        QVERIFY(label && !label->isVisible());
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        include->trigger();
        QVERIFY(!w.studioDocument().audio);
        QVERIFY(label->isVisible());
        QCOMPARE(undo->count(), steps + 1);
        undo->undo();
        QVERIFY(w.studioDocument().audio);
        QVERIFY(include->isChecked() && !label->isVisible());
        // Without sound there is nothing to leave out.
        EditorWindow silent(videoDoc(dir.filePath("none.mp4")), cfg, {});
        for (QAction *a : silent.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Include audio in output")) QVERIFY(!a->isEnabled());
    }
    void theSoundComesOffInTheExportPanelToo() {
        // Where output settings live: one switch with the speaker's menu, the same undo step.
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow w(doc, cfg, {});
        w.show();
        ExportPanel *panel = nullptr;
        for (auto *a : w.findChildren<QWidgetAction *>())
            if (auto *p = qobject_cast<ExportPanel *>(a->defaultWidget())) panel = p;
        QVERIFY(panel);
        QAbstractButton *on = nullptr, *off = nullptr;
        for (auto *b : panel->findChildren<QAbstractButton *>()) {
            if (b->accessibleName() == QStringLiteral("Sound On")) on = b;
            if (b->accessibleName() == QStringLiteral("Sound Off")) off = b;
        }
        QVERIFY(on && off && on->isVisibleTo(panel) && on->isChecked());
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        off->click();
        QVERIFY(!w.studioDocument().audio);
        QVERIFY(off->isChecked());
        QVERIFY(w.findChild<QLabel *>(QStringLiteral("NoAudio"))->isVisible());
        QCOMPARE(undo->count(), steps + 1);
        undo->undo();
        QVERIFY(w.studioDocument().audio && on->isChecked());
        // A clip without sound has no row; a GIF has no sound to carry.
        EditorWindow silent(videoDoc(dir.filePath("none.mp4")), cfg, {});
        for (auto *a : silent.findChildren<QWidgetAction *>())
            if (auto *p = qobject_cast<ExportPanel *>(a->defaultWidget()))
                for (auto *b : p->findChildren<QAbstractButton *>())
                    if (b->accessibleName().startsWith(QStringLiteral("Sound "))) QVERIFY(!b->isVisibleTo(p));
        ExportSettings gif = panel->settings();
        gif.format = ExportSettings::Format::Gif;
        panel->setSettings(gif);
        QVERIFY(off->isChecked() && !on->isEnabled());
    }
    void buttonMenusOpenOnRightClickAndSaySo() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow w(doc, cfg, {});
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        for (const char *name : {"PlaybackMute", "Copy", "Save"}) {
            auto *button = w.findChild<QToolButton *>(QString::fromLatin1(name));
            QVERIFY2(button && button->menu(), name);
            QVERIFY2(button->toolTip().contains(QStringLiteral("Right-click")), name);
            QSignalSpy opened(button->menu(), &QMenu::aboutToShow);
            QSignalSpy clicked(button, &QToolButton::clicked);
            QTimer::singleShot(0, button->menu(), &QMenu::close);
            QContextMenuEvent event(QContextMenuEvent::Mouse, button->rect().center(),
                                    button->mapToGlobal(button->rect().center()));
            QApplication::sendEvent(button, &event);
            QCOMPARE(opened.count(), 1);
            QCOMPARE(clicked.count(), 0);
        }
    }
    void leavingTheSoundOutMovesNothing() {
        // "No audio" used to appear inside the transport row and push it aside.
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow w(doc, cfg, {});
        w.resize(1000, 680);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        const auto at = [&](const char *name) {
            auto *widget = w.findChild<QWidget *>(QString::fromLatin1(name));
            return QRect(widget->mapTo(&w, QPoint()), widget->size());
        };
        const QRect mute = at("PlaybackMute"), loop = at("PlaybackLoop"), pill = at("DragPill"), trim = at("TrimControls");
        QAction *include = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text() == QStringLiteral("Include audio in output")) include = a;
        include->trigger();
        QApplication::processEvents();
        auto *label = w.findChild<QLabel *>(QStringLiteral("NoAudio"));
        QVERIFY(label->isVisible());
        QCOMPARE(label->parentWidget()->objectName(), QStringLiteral("ViewControls"));   // the footer
        QCOMPARE(w.width() - 1 - at("NoAudio").right(), theme::kBarPad);                 // on the window's edge
        QCOMPARE(at("PlaybackMute"), mute);
        QCOMPARE(at("PlaybackLoop"), loop);
        QCOMPARE(at("DragPill"), pill);
        QCOMPARE(at("TrimControls"), trim);
    }
    void aSlowedClipsDurationStillFits() {
        // Duration shows the output: at 0.25x a 2:40 clip runs 10:40, a digit longer than its source.
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4"), 160000), cfg, {});
        w.show();
        StudioDocument doc = w.studioDocument();
        Fragment slow;
        slow.speed = 0.25;
        doc.fragments = {slow};
        w.setStudioDocument(doc);
        auto *duration = w.findChild<QLineEdit *>(QStringLiteral("TrimDuration"));
        QCOMPARE(duration->text(), QStringLiteral("10:40.000"));
        QVERIFY2(duration->contentsRect().width() >= duration->fontMetrics().horizontalAdvance(duration->text()),
                 qPrintable(QString::number(duration->contentsRect().width())));
    }
    void aSilentTrackOffersNoWaveformToShow() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("silent.mkv"));
        QProcess make;
        make.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", "color=c=black:s=64x48:d=2:r=25",
            "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo:d=2", "-c:a", "pcm_s16le", "-shortest", file});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        VideoTimeline timeline;
        timeline.resize(400, VideoTimeline::kBaseH);
        timeline.setDuration(2000);
        AudioWaveformProvider waveform(file, 2000, 0);
        timeline.setWaveform(&waveform);
        timeline.show();
        QTRY_VERIFY(waveform.silent());
        QTimer::singleShot(0, &timeline, [&] {
            auto *menu = QApplication::activePopupWidget();
            QVERIFY(menu);
            for (QAction *a : static_cast<QMenu *>(menu)->actions())
                QVERIFY(a->text() != QStringLiteral("Show waveform"));
            menu->close();
        });
        emit timeline.customContextMenuRequested(QPoint(200, 20));
    }
    void theSoundMenuTogglesFromTheKeyboardAndStaysOpen() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow w(doc, cfg, {});
        w.show();
        QMenu *menu = w.findChild<QToolButton *>(QStringLiteral("PlaybackMute"))->menu();
        QAction *include = nullptr;
        for (QAction *a : menu->actions())
            if (a->text() == QStringLiteral("Include audio in output")) include = a;
        menu->popup(w.mapToGlobal(QPoint(100, 100)));
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        menu->setActiveAction(include);
        QTest::keyClick(menu, Qt::Key_Return);
        QVERIFY(!w.studioDocument().audio);
        QVERIFY(menu->isVisible());
        QTest::keyClick(menu, Qt::Key_Space);
        QVERIFY(w.studioDocument().audio);
        menu->close();
    }
    void theSoundMenuStaysOpenWhileItToggles() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath("none.mp4"));
        doc.video.hasAudio = true;
        EditorWindow w(doc, cfg, {});
        w.show();
        QMenu *menu = w.findChild<QToolButton *>(QStringLiteral("PlaybackMute"))->menu();
        QAction *include = nullptr;
        for (QAction *a : menu->actions())
            if (a->text() == QStringLiteral("Include audio in output")) include = a;
        QVERIFY(include);
        menu->popup(w.mapToGlobal(QPoint(100, 100)));
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(include).center());
        QVERIFY(!include->isChecked() && !w.studioDocument().audio);
        QVERIFY(menu->isVisible());
        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(include).center());
        QVERIFY(include->isChecked() && w.studioDocument().audio);
        menu->close();
    }
    void aScrubLeavesFragmentsAloneAndAClickPicksOne() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.show();
        StudioDocument doc = w.studioDocument();
        Fragment second;
        second.startMs = 2000;
        doc.fragments = {Fragment(), second};
        w.setStudioDocument(doc);
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *bar = w.findChild<FragmentBar *>();
        emit timeline->interactionStarted(false);
        emit timeline->interactionFinished(false);   // a scrub
        QVERIFY(!bar->isVisible());
        timeline->setPosition(3000);
        emit timeline->stripClicked(3000);
        QTRY_VERIFY(bar->isVisible());
        QCOMPARE(w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->text(), QStringLiteral("Cut"));
    }
    void deleteTogglesACutAndTheNotchRestores() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *bar = w.findChild<FragmentBar *>();
        auto *undo = w.findChild<QUndoStack *>();
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_S);   // the part after the split is selected
        const int steps = undo->count();
        QTest::keyClick(&w, Qt::Key_Delete);
        QVERIFY(w.studioDocument().fragments[1].removed);
        QCOMPARE(w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->text(), QStringLiteral("Restore"));
        QTest::keyClick(&w, Qt::Key_Delete);   // used to be swallowed on a cut piece
        QVERIFY(w.studioDocument().fragments.isEmpty());
        QTRY_VERIFY(!bar->isVisible());
        QCOMPARE(undo->count(), steps + 2);
        undo->undo();
        QVERIFY(w.studioDocument().fragments[1].removed);
        emit timeline->cutClicked(1);   // the notch: restore, as its hint says
        QVERIFY(w.studioDocument().fragments.isEmpty());
        QCOMPARE(undo->count(), steps + 2);
        QVERIFY(!bar->isVisible());
    }
    void markingARangeAndDeleteCutsItInOneStep() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *bar = w.findChild<FragmentBar *>();
        const auto f = [](qint64 start, bool removed) { Fragment x; x.startMs = start; x.removed = removed; return x; };
        const int steps = undo->count();
        timeline->setMark(1000, 2000);
        QTest::keyClick(&w, Qt::Key_Delete);
        QCOMPARE(w.studioDocument().fragments, (QVector<Fragment>{f(0, false), f(1000, true), f(2000, false)}));
        QCOMPARE(w.findChild<QLineEdit *>(QStringLiteral("TrimDuration"))->text(), QStringLiteral("0:03.000"));
        QCOMPARE(undo->count(), steps + 1);
        QVERIFY(!timeline->hasMark());
        QCOMPARE(timeline->position(), 2000);   // the first kept frame after the cut
        QVERIFY(!bar->isVisible());
        undo->undo();
        QVERIFY(w.studioDocument().fragments.isEmpty());
        // From the keyboard: Shift+I, the playhead moves on, Delete.
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_I, Qt::ShiftModifier);
        timeline->setPosition(2500);
        QTest::keyClick(&w, Qt::Key_Delete);
        QCOMPARE(w.studioDocument().fragments.size(), 3);
        QCOMPARE(w.studioDocument().fragments[1], f(1000, true));
        QCOMPARE(w.studioDocument().fragments[2].startMs, 2500);
        QCOMPARE(timeline->trimIn(), 0);   // Shift+I marks, it does not trim
        // Esc lets go of a mark before anything else.
        timeline->setMark(500, 900);
        QTest::keyClick(&w, Qt::Key_Escape);
        QVERIFY(!timeline->hasMark());
        QVERIFY(w.isVisible());
        // A cut that would keep nothing is refused, the mark stays.
        const int before = undo->count();
        timeline->setMark(0, 4000);
        QTest::keyClick(&w, Qt::Key_Delete);
        QCOMPARE(w.findChild<Toast *>()->text(), QStringLiteral("Keep at least part of the clip"));
        QCOMPARE(undo->count(), before);
        QVERIFY(timeline->hasMark());
    }
    void splitSpeedCutAndJoinAreUndoSteps() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.resize(900, 640);
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *duration = w.findChild<QLineEdit *>(QStringLiteral("TrimDuration"));
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
        // Esc lets go of the fragment before it would close the window; a click picks it again.
        QTest::keyClick(&w, Qt::Key_Escape);
        QTRY_VERIFY(!bar->isVisible());
        QVERIFY(w.isVisible());
        emit timeline->stripClicked(2000);
        QTRY_VERIFY(bar->isVisible());
        w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->click();
        QVERIFY(w.studioDocument().fragments[1].removed);
        QCOMPARE(duration->text(), QStringLiteral("0:01.000"));
        QCOMPARE(w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->text(), QStringLiteral("Restore"));
        w.findChild<QToolButton *>(QStringLiteral("FragmentCut"))->click();
        QVERIFY(!w.studioDocument().fragments[1].removed);
        QCOMPARE(undo->count(), steps + 3);
        QTRY_VERIFY(!bar->isVisible());   // restored, it may melt into its neighbour: nothing stays selected
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
    void fromPlayheadLimitsARedactionAndShowsItOnTheMaskLane() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.resize(900, 640);
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *redact = new RedactItem(RedactMode::Blacken, QImage(160, 90, QImage::Format_ARGB32_Premultiplied),
                                      QRectF(10, 10, 40, 30));
        redact->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene->addItem(redact);
        redact->setSelected(true);
        auto *bar = w.findChild<RedactBar *>();
        QTRY_VERIFY(bar->isVisible());
        QToolButton *fromPlayhead = nullptr, *wholeClip = nullptr;
        for (auto *b : bar->findChildren<QToolButton *>(QStringLiteral("TimeScope"))) {
            if (b->text() == QStringLiteral("From playhead")) fromPlayhead = b;
            if (b->text() == QStringLiteral("Whole clip")) wholeClip = b;
        }
        QVERIFY(fromPlayhead && wholeClip && fromPlayhead->isVisible());
        QVERIFY(wholeClip->isChecked());
        timeline->setPosition(2000);
        const int steps = undo->count();
        fromPlayhead->click();
        QCOMPARE(redact->timeWindow(), (AnnotationItem::TimeWindow{{2000, 4000}}));
        QCOMPARE(undo->count(), steps + 1);
        QVERIFY(!timeline->maskLaneRect().isEmpty());
        QVERIFY(redact->isVisible());
        // Outside its window the redaction is not shown.
        undo->undo();
        QVERIFY(timeline->maskLaneRect().isEmpty());
        timeline->setPosition(500);
        undo->redo();
        QVERIFY(!redact->isVisible());
        // Its block on the mask lane brings it back into view.
        emit timeline->maskSelected(quintptr(redact));
        QCOMPARE(timeline->position(), 2000);
        QVERIFY(redact->isVisible() && redact->isSelected());
        wholeClip->click();
        QVERIFY(!redact->timeWindow());
        // At the very end the window still gets a length, or the project could not be read again.
        timeline->setPosition(4000);
        fromPlayhead->click();
        QCOMPARE(redact->timeWindow(), (AnnotationItem::TimeWindow{{3900, 4000}}));
    }
};

QTEST_MAIN(TestFragmentsEdit)
#include "test_fragmentsedit.moc"

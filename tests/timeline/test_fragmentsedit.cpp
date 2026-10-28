#include <QtTest>
#include <QAudioOutput>
#include <QGraphicsVideoItem>
#include <QLabel>
#include <QLineEdit>
#include <QGraphicsRectItem>
#include <QElapsedTimer>
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
#include "timeline/bars/fragmentbar.h"
#include "timeline/videotimeline.h"
#include <QTimer>
#include "timeline/lanes/audiowaveform.h"
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
        const QRect mute = at("PlaybackMute"), loop = at("PlaybackLoop"), pill = at("DragPill"), trim = at("ClipControls");
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
        QCOMPARE(at("ClipControls"), trim);
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
        auto *duration = w.findChild<QLabel *>(QStringLiteral("ClipDuration"));
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
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("ClipDelete"))->isEnabled());   // a chosen piece can go
    }
    void deleteLeavesAGhostAndADoubleClickBringsItBack() {
        // K, G: Delete leaves the chosen piece out; it stays as a ghost where it was, nothing
        // moves, and a double click on the ghost restores it.
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *bar = w.findChild<FragmentBar *>();
        auto *undo = w.findChild<QUndoStack *>();
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_S);
        timeline->setPosition(2000);
        QTest::keyClick(&w, Qt::Key_S);   // 0-1, 1-2 and 2-4 s
        emit timeline->stripClicked(1500);
        QTRY_VERIFY(bar->isVisible());
        const int steps = undo->count();
        const QVector<QRectF> before = timeline->pieceRects();
        QTest::keyClick(&w, Qt::Key_Delete);
        QVERIFY(w.studioDocument().fragments[1].removed);
        QCOMPARE(timeline->pieceRects().size(), 2);
        QCOMPARE(timeline->pieceRects()[0], before[0]);   // nothing closes up
        QCOMPARE(timeline->pieceRects()[1], before[2]);
        QCOMPARE(timeline->ghostRects().size(), 1);
        QVERIFY(qAbs(timeline->ghostRects()[0].center().x() - before[1].center().x()) <= 0.5);
        QCOMPARE(timeline->position(), 2000);   // where the next piece starts
        QTRY_VERIFY(!bar->isVisible());
        QCOMPARE(undo->count(), steps + 1);
        QVERIFY(!w.findChild<QToolButton *>(QStringLiteral("ClipDelete"))->isEnabled());   // nothing chosen
        QTest::mouseDClick(timeline, Qt::LeftButton, Qt::NoModifier, timeline->ghostRects()[0].center().toPoint());
        const auto parts = w.studioDocument().fragments;
        QVERIFY(std::none_of(parts.cbegin(), parts.cend(), [](const Fragment &f) { return f.removed; }));
        QCOMPARE(undo->count(), steps + 2);
        // The last piece goes by its trim: the end moves onto the piece before.
        timeline->setPosition(3000);
        QTest::keyClick(&w, Qt::Key_S);
        QTest::keyClick(&w, Qt::Key_Delete);
        QCOMPARE(timeline->trimOut(), 3000);
        // And the only piece stays.
        for (int i = 0; i < 6; ++i) {
            emit timeline->stripClicked(timeline->trimIn());
            QTest::keyClick(&w, Qt::Key_Delete);
        }
        QCOMPARE(timeline->pieceRects().size(), 1);
        QCOMPARE(w.findChild<Toast *>()->text(), QStringLiteral("Keep at least part of the clip"));
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
        QCOMPARE(w.findChild<QLabel *>(QStringLiteral("ClipDuration"))->text(), QStringLiteral("0:03.000"));
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
    void theFragmentBarKeepsOneWidth() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *bar = w.findChild<FragmentBar *>();
        timeline->setPosition(1000);
        QTest::keyClick(&w, Qt::Key_S);
        QTRY_VERIFY(bar->isVisible());
        const int kept = bar->width();
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("FragmentJoin"))->isEnabled());
        emit timeline->stripClicked(500);   // the first piece: nothing before it to join
        QCOMPARE(bar->width(), kept);
        QVERIFY(!w.findChild<QToolButton *>(QStringLiteral("FragmentJoin"))->isEnabled());
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("FragmentJoin"))->isVisible());
    }
    void cutPausesFindsTheSilenceWhileThePointerRests() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("pause.mkv"));
        QProcess make;
        make.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", "color=c=gray:s=160x90:d=7:r=25",
            "-f", "lavfi", "-i", "sine=f=440:d=7", "-af", "volume='if(between(t,2,5),0,1)':eval=frame",
            "-c:v", "libx264", "-c:a", "pcm_s16le", "-shortest", file});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        MediaDocument doc = videoDoc(file, 7000);
        doc.video.hasAudio = true;
        CursorTrack track;
        track.videoSize = QSize(160, 90);
        track.samples = {{0, QPointF(80, 45), true}};
        doc.cursorTrack = track;
        Config cfg; cfg.animations = false;
        EditorWindow w(doc, cfg, {});
        w.show();
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        emit w.findChild<VideoTimeline *>()->pausesRequested(true);
        QTRY_VERIFY_WITH_TIMEOUT(!w.studioDocument().fragments.isEmpty(), 10000);
        const QVector<Fragment> cut = w.studioDocument().fragments;
        QCOMPARE(cut.size(), 3);
        QVERIFY2(cut[1].removed && qAbs(cut[1].startMs - 2200) <= 60, qPrintable(QString::number(cut[1].startMs)));
        QVERIFY2(qAbs(cut[2].startMs - 4800) <= 60, qPrintable(QString::number(cut[2].startMs)));
        QCOMPARE(undo->count(), steps + 1);   // one step
        QVERIFY(w.findChild<Toast *>()->text().startsWith(QStringLiteral("Cut 1 pause,")));
        undo->undo();
        QVERIFY(w.studioDocument().fragments.isEmpty());
    }
    void withoutSoundOrPointerTheFrozenPictureDecides() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("frozen.mp4"));
        QProcess make;   // 3 s of a still picture, then 3 s that move
        make.start(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i", "color=c=gray:s=160x90:d=3:r=25",
            "-f", "lavfi", "-i", "testsrc2=s=160x90:d=3:r=25",
            "-filter_complex", "[0:v][1:v]concat=n=2:v=1[v]", "-map", "[v]", "-c:v", "libx264", file});
        QVERIFY(make.waitForFinished(30000) && make.exitCode() == 0);
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(file, 6000), cfg, {});
        w.show();
        emit w.findChild<VideoTimeline *>()->pausesRequested(true);
        QTRY_VERIFY_WITH_TIMEOUT(!w.studioDocument().fragments.isEmpty(), 15000);
        const QVector<Fragment> cut = w.studioDocument().fragments;
        QVERIFY(cut[0].removed);   // from the clip's start: no breath kept there
        QVERIFY2(qAbs(cut[1].startMs - 2800) <= 150, qPrintable(QString::number(cut[1].startMs)));
    }
    void splitSpeedDeleteAndJoinAreUndoSteps() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath("none.mp4")), cfg, {});
        w.resize(900, 640);
        w.show();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *duration = w.findChild<QLabel *>(QStringLiteral("ClipDuration"));
        timeline->setPosition(1000);
        w.findChild<QToolButton *>(QStringLiteral("ClipSplit"))->click();   // the button, as S
        QCOMPARE(w.studioDocument().fragments.size(), 2);
        QCOMPARE(w.studioDocument().fragments[1].startMs, 1000);
        auto *bar = w.findChild<FragmentBar *>();
        QTRY_VERIFY(bar->isVisible());                     // the part after the split is chosen
        const int steps = undo->count();
        w.findChild<QMenu *>(QStringLiteral("FragmentSpeedMenu"))->actions().at(4)->trigger();   // 2x
        QCOMPARE(w.studioDocument().fragments[1].speed, 2.0);
        QCOMPARE(duration->text(), QStringLiteral("0:02.500"));   // 1 s + 3 s at 2x
        // Esc lets go of the piece before it would close the window; a click picks it again.
        QTest::keyClick(&w, Qt::Key_Escape);
        QTRY_VERIFY(!bar->isVisible());
        QVERIFY(w.isVisible());
        emit timeline->stripClicked(2000);
        QTRY_VERIFY(bar->isVisible());
        w.findChild<QToolButton *>(QStringLiteral("ClipDelete"))->click();
        QCOMPARE(timeline->trimOut(), 1000);   // the last piece: the trim takes it
        QCOMPARE(duration->text(), QStringLiteral("0:01.000"));
        QCOMPARE(undo->count(), steps + 2);
        undo->undo();
        QCOMPARE(timeline->trimOut(), 4000);
        QCOMPARE(duration->text(), QStringLiteral("0:02.500"));
        emit timeline->stripClicked(2000);
        w.findChild<QToolButton *>(QStringLiteral("FragmentJoin"))->click();
        QVERIFY(w.studioDocument().fragments.isEmpty());   // joined, it plays at the first one's 1x
        QCOMPARE(duration->text(), QStringLiteral("0:04.000"));
        QCOMPARE(undo->count(), steps + 2);
        undo->undo();
        undo->undo();
        undo->undo();
        QVERIFY(w.studioDocument().fragments.isEmpty());
        QCOMPARE(duration->text(), QStringLiteral("0:04.000"));
    }
    // P: playback holds through a pause: the playhead runs through it, the player waits, then on.
    void playbackHoldsThroughAPause() {
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
        auto *timeline = w.findChild<VideoTimeline *>();
        for (const bool black : {false, true}) {
            StudioDocument doc;
            doc.fragments = {{0, 1.0, false}, {1000, 1.0, true}, {2000, 1.0, false, 600, black}};
            w.setStudioDocument(doc);
            timeline->setPosition(0);
            emit timeline->seekRequested(0);   // as a click there would
            QTRY_VERIFY(player->position() < 100);
            QElapsedTimer clock;
            qint64 pauseFrom = -1, pauseTo = -1, greenAt = -1;
            bool coveredInPause = false, coveredAfter = false;
            QTimer poll;
            connect(&poll, &QTimer::timeout, &w, [&] {
                const bool inPause = timeline->pausePosition() >= 0;
                if (inPause && pauseFrom < 0) pauseFrom = clock.elapsed();
                if (inPause) pauseTo = clock.elapsed();
                QGraphicsRectItem *cover = nullptr;
                for (QGraphicsItem *child : video->childItems())
                    if (auto *r = dynamic_cast<QGraphicsRectItem *>(child)) cover = r;
                if (inPause && cover && cover->isVisible()) coveredInPause = true;
                if (greenAt >= 0 && cover && cover->isVisible()) coveredAfter = true;
                const QColor c = video->videoSink()->videoFrame().toImage().pixelColor(80, 45);
                if (greenAt < 0 && pauseTo >= 0 && c.green() > 150 && c.red() < 100) greenAt = clock.elapsed();
            });
            clock.start();
            poll.start(10);
            QTest::mouseClick(w.findChild<QToolButton *>(QStringLiteral("PlaybackPlay")), Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(greenAt >= 0, 8000);
            poll.stop();
            player->pause();
            qInfo("black %d: pause shown %lld to %lld ms, green at %lld ms", black, pauseFrom, pauseTo, greenAt);
            QVERIFY(pauseFrom >= 0);
            QVERIFY2(pauseTo - pauseFrom >= 450, qPrintable(QString::number(pauseTo - pauseFrom)));   // 600 ms, polled
            QVERIFY(greenAt >= pauseTo);
            QCOMPARE(coveredInPause, black);
            QVERIFY(!coveredAfter);
        }
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
    // G: a click on a ghost shows what was cut there; it splits nothing and chooses nothing;
    // playing from it goes on with the next kept piece.
    void aGhostShowsItsFramesAndPlaysOnPastThem() {
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
        auto *timeline = w.findChild<VideoTimeline *>();
        StudioDocument doc;
        doc.fragments = {{0, 1.0, false}, {1000, 1.0, true}, {2000, 1.0, false}};   // blue is cut
        w.setStudioDocument(doc);
        QCOMPARE(timeline->ghostRects().size(), 1);
        const QPoint ghost = timeline->ghostRects()[0].center().toPoint();
        QTest::mouseClick(timeline, Qt::LeftButton, Qt::NoModifier, ghost);
        const auto colour = [&] { return video->videoSink()->videoFrame().toImage().pixelColor(80, 45); };
        QTRY_VERIFY2(colour().blue() > 150 && colour().red() < 100, qPrintable(colour().name()));   // its frame
        QVERIFY(!w.findChild<FragmentBar *>()->isVisible());   // nothing chosen
        QTest::keyClick(&w, Qt::Key_S);
        QCOMPARE(w.findChild<Toast *>()->text(), QStringLiteral("Nothing to split here"));
        QCOMPARE(w.studioDocument().fragments, doc.fragments);
        QTest::mouseClick(w.findChild<QToolButton *>(QStringLiteral("PlaybackPlay")), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(colour().green() > 150 && colour().blue() < 100, 4000);   // on with green
        QVERIFY(player->position() >= 2000);
        player->pause();
    }
};

QTEST_MAIN(TestFragmentsEdit)
#include "test_fragmentsedit.moc"

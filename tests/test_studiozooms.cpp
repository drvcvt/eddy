#include <QtTest>
#include <QApplication>
#include <QAudioOutput>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QMediaPlayer>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>
#include <QToolButton>
#include <QUndoStack>
#include <QVideoSink>
#include "canvas.h"
#include "editorwindow.h"
#include "minimap.h"
#include "studiopopover.h"
#include "videotimeline.h"
#include "zoombar.h"
#include "studiopresets.h"
#include "exportpanel.h"
#include "exportsettings.h"
#include <QDir>
#include "studiostyle.h"
#include <QLabel>

using namespace eddy;

static bool have(const QString &cmd) { return !QStandardPaths::findExecutable(cmd).isEmpty(); }

static bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// 4 s, 320x180: a grey grid with a red marker at (240, 60).
static QString markerClip(const QTemporaryDir &dir) {
    QImage image(320, 180, QImage::Format_RGB32);
    image.fill(QColor(60, 60, 60));
    {
        QPainter p(&image);
        p.setPen(QColor(120, 120, 120));
        for (int x = 0; x < 320; x += 20) p.drawLine(x, 0, x, 179);
        for (int y = 0; y < 180; y += 20) p.drawLine(0, y, 319, y);
        p.fillRect(236, 56, 8, 8, Qt::red);
    }
    const QString still = dir.filePath(QStringLiteral("marker.png"));
    image.save(still);
    const QString clip = dir.filePath(QStringLiteral("marker.mp4"));
    runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-loop", "1", "-i", still, "-t", "4",
        "-r", "30", "-vf", "setsar=1,format=yuv420p", "-c:v", "libx264", "-g", "15", clip});
    return clip;
}

static MediaDocument videoDoc(const QString &path) {
    MediaDocument doc;
    doc.kind = MediaKind::Video;
    doc.path = path;
    doc.video = {QSize(320, 180), 4000, 30.0};
    return doc;
}

static StudioStyle dusk() {
    StudioStyle s;
    s.background = StudioStyle::Background::Color;
    s.color = QColor(40, 40, 120);
    return s;
}

class TestStudioZooms : public QObject {
    Q_OBJECT
private slots:
    void laneFollowsStudio() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        auto *timeline = w.findChild<VideoTimeline *>();
        QVERIFY(!timeline->zoomLaneVisible());
        w.setStudioStyle(dusk());
        QVERIFY(timeline->zoomLaneVisible());
        w.setStudioStyle(StudioStyle());
        QVERIFY(!timeline->zoomLaneVisible());
    }
    void playbackRowMakesRoomForTheLane() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(1000, 680);
        w.show();
        w.setStudioStyle(dusk());
        QApplication::processEvents();
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *play = w.findChild<QToolButton *>(QStringLiteral("PlaybackPlay"));
        QTRY_COMPARE(timeline->height(), 84);
        const QRect lane = QRect(timeline->mapTo(&w, QPoint()), timeline->size());
        const QRect button = QRect(play->mapTo(&w, QPoint()), play->size());
        QVERIFY2(!lane.intersects(button), qPrintable(QStringLiteral("timeline %1..%2, play %3..%4")
            .arg(lane.top()).arg(lane.bottom()).arg(button.top()).arg(button.bottom())));
    }
    void zAddsASelectedZoomInOneUndoStep() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        auto *undo = w.findChild<QUndoStack *>();
        const int before = undo->count();
        QTest::keyClick(&w, Qt::Key_Z);
        const StudioDocument doc = w.studioDocument();
        QCOMPARE(doc.zooms.size(), 1);
        QCOMPARE(doc.zooms.first().startMs, 0);
        QCOMPARE(doc.zooms.first().endMs, 2000);
        QCOMPARE(doc.zooms.first().scale, 2.0);
        QCOMPARE(doc.zooms.first().point, QPointF(160, 90));
        QCOMPARE(w.selectedZoom(), doc.zooms.first().id);
        QCOMPARE(undo->count(), before + 1);
        QVERIFY(w.findChild<VideoTimeline *>()->zoomLaneVisible());
        QTRY_VERIFY(w.findChild<ZoomBar *>()->isVisible());
        // The canvas shows where the zoom settles, not the whole frame.
        const QRectF target(80, 45, 160, 90);
        QCOMPARE(w.findChild<Canvas *>()->camera(), target);
        undo->undo();
        QVERIFY(w.studioDocument().zooms.isEmpty());
        QCOMPARE(w.selectedZoom(), 0u);
        QVERIFY(!w.findChild<ZoomBar *>()->isVisible());
        QVERIFY(w.findChild<Canvas *>()->camera().isEmpty());
    }
    void escapeDeselectsBeforeClosingAndDeleteRemoves() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        QVERIFY(w.selectedZoom());
        QTest::keyClick(&w, Qt::Key_Escape);
        QCOMPARE(w.selectedZoom(), 0u);
        QVERIFY(w.isVisible());
        w.selectZoom(w.studioDocument().zooms.first().id);
        QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);   // ten frames at 30 fps
        QCOMPARE(w.studioDocument().zooms.first().startMs, 333);
        QTest::keyClick(&w, Qt::Key_Delete);
        QVERIFY(w.studioDocument().zooms.isEmpty());
    }
    void zoomBarAndMiniMapEditTheSelectedZoom() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        w.findChild<QMenu *>(QStringLiteral("ZoomScaleMenu"))->actions().at(3)->trigger();
        QCOMPARE(w.studioDocument().zooms.first().scale, 3.0);
        w.findChild<QMenu *>(QStringLiteral("ZoomMotionMenu"))->actions().at(1)->trigger();
        QCOMPARE(w.studioDocument().zooms.first().motion, ZoomSegment::Motion::Smooth);
        QCOMPARE(undo->count(), steps + 2);
        auto *map = w.findChild<MiniMap *>();
        QTRY_VERIFY(map->isVisible());
        // Two wheel notches, one undo step.
        emit map->wheelZoom(-1);
        emit map->wheelZoom(-1);
        QVERIFY(qAbs(w.studioDocument().zooms.first().scale - 3.0 / 1.21) < 1e-9);
        QCOMPARE(undo->count(), steps + 3);
        // Dragging the map moves the target with it, clamped like the camera.
        emit map->dragged(QPointF(40, 0));
        emit map->dragFinished(false);
        QCOMPARE(w.studioDocument().zooms.first().point.x(), 200.0);
        QCOMPARE(undo->count(), steps + 4);
        w.findChild<QToolButton *>(QStringLiteral("ZoomRemove"))->click();
        QVERIFY(w.studioDocument().zooms.isEmpty());
    }
    void canvasDragMovesTheTargetAgainstThePointer() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        QTest::keyClick(&w, Qt::Key_M);   // the Move tool steers; the others draw as always
        auto *canvas = w.findChild<Canvas *>();
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        const QPoint start = canvas->mapFromScene(QPointF(160, 90));
        const double scale = canvas->transform().m11();
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas->viewport(), start + QPoint(-20, 0));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(-20, 0));
        // The content follows the pointer, so the camera moved right.
        QVERIFY(qAbs(w.studioDocument().zooms.first().point.x() - (160 + 20 / scale)) < 0.5);
        QCOMPARE(undo->count(), steps + 1);
    }
    void cameraPageSetsEveryZoomAndKeepZoomedInNarrowsTheView() {
        QTemporaryDir dir;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, cli);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        StudioDocument doc = w.studioDocument();
        doc.style = dusk();
        doc.style.aspect = QSize(9, 16);
        w.setStudioDocument(doc);
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        w.openStudio();
        auto *popover = w.findChild<StudioPopover *>();
        QVERIFY(popover);
        for (auto *b : popover->findChildren<QToolButton *>(QStringLiteral("StudioMotion")))
            if (b->text() == QStringLiteral("Instant")) b->click();
        auto *keep = popover->findChild<QToolButton *>(QStringLiteral("StudioKeepZoomed"));
        QVERIFY(keep->isEnabled());
        keep->click();
        popover->close();
        QTRY_VERIFY(!w.findChild<StudioPopover *>());
        QCOMPARE(undo->count(), steps + 1);
        QCOMPARE(w.studioDocument().motion, ZoomSegment::Motion::Instant);
        QCOMPARE(w.studioDocument().zooms.first().motion, ZoomSegment::Motion::Instant);
        QVERIFY(w.studioDocument().keepZoomedIn);
        const QRect base = w.cameraBase();
        QCOMPARE(base.height(), 180);
        QVERIFY(base.width() < 120);
        QCOMPARE(w.findChild<Canvas *>()->contentRect(), QRectF(base));
    }
    void cameraPageShowsTheZoomsSharedMotionAndTheFramedSize() {
        QTemporaryDir dir;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, cli);
        w.show();
        StudioDocument doc;
        doc.style = dusk();
        doc.style.aspect = QSize(9, 16);
        doc.keepZoomedIn = true;
        doc.keepCenter = QPointF(160, 90);
        doc.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(160, 90), ZoomSegment::Motion::Smooth}};
        w.setStudioDocument(doc);   // default motion stays Focused
        w.openStudio();
        auto *popover = w.findChild<StudioPopover *>();
        QVERIFY(popover);
        for (auto *b : popover->findChildren<QToolButton *>(QStringLiteral("StudioMotion")))
            QCOMPARE(b->isChecked(), b->text() == QStringLiteral("Smooth"));
        // The size label shows the framed base view, the size the export will have.
        const QSize out = studioLayout(w.cameraBase().size(), doc.style).output;
        QCOMPARE(popover->findChild<QLabel *>(QStringLiteral("StudioSize"))->text(),
                 QStringLiteral("%1 × %2").arg(out.width()).arg(out.height()));
        popover->close();
    }
    void undoDuringALaneDragCancelsTheDrag() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
        w.resize(900, 640);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);                 // 0..2000
        auto *timeline = w.findChild<VideoTimeline *>();
        QTRY_VERIFY(timeline->zoomLaneVisible());
        const QRectF lane = timeline->zoomLaneRect();
        const int y = int(lane.center().y());
        auto xAt = [&](qint64 ms) { return int(lane.left() + lane.width() * ms / 4000.0); };
        QTest::mousePress(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(1000), y));
        QTest::mouseMove(timeline, QPoint(xAt(1500), y));
        QTest::mouseMove(timeline, QPoint(xAt(2000), y));
        QVERIFY(w.studioDocument().zooms.first().startMs > 0);
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);   // takes the zoom away again
        QTest::mouseRelease(timeline, Qt::LeftButton, Qt::NoModifier, QPoint(xAt(2000), y));
        QVERIFY2(w.studioDocument().zooms.isEmpty(), "the release brought the undone zoom back");
    }
    void exportPresetsReachEverySaveRoute() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = markerClip(dir);
        Config cfg; cfg.animations = false; cfg.copyOnSave = false;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        cli.output.saveDir = dir.path();
        EditorWindow w(videoDoc(clip), cfg, cli);
        w.show();
        auto *menu = w.findChild<QMenu *>(QStringLiteral("ExportMenu"));
        QVERIFY(menu);
        auto *panel = menu->findChild<ExportPanel *>();
        QVERIFY(panel);
        emit menu->aboutToShow();
        QCOMPARE(panel->findChild<QLabel *>(QStringLiteral("ExportSummary"))->text(),
                 QStringLiteral("320 × 180   0:04"));
        for (auto *b : panel->findChildren<QToolButton *>(QStringLiteral("ExportSegment")))
            if (b->accessibleName() == QStringLiteral("Preset GIF")) b->click();
        // Remembered for the next window.
        QCOMPARE(loadExportSettings(cli.configPath), exportPreset(ExportPreset::Gif));
        panel->findChild<QToolButton *>(QStringLiteral("ExportSave"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(!QDir(dir.path()).entryList({QStringLiteral("eddy-*.gif")}).isEmpty(), 20000);
        const QString gif = dir.filePath(QDir(dir.path()).entryList({QStringLiteral("eddy-*.gif")}).first());
        QProcess probe;
        probe.start(QStringLiteral("ffprobe"), {"-v", "error", "-show_entries", "stream=codec_name,width,height",
                                               "-of", "csv=p=0", gif});
        QVERIFY(probe.waitForFinished(10000));
        QCOMPARE(probe.readAllStandardOutput().trimmed(), QByteArray("gif,320,180"));
    }
    void followCursorNeedsATrackAndFollowsIt() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        {
            EditorWindow plain(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, {});
            plain.show();
            QTest::keyClick(&plain, Qt::Key_Z);
            QToolButton *cursor = nullptr;
            for (auto *b : plain.findChildren<QToolButton *>(QStringLiteral("ZoomTarget")))
                if (b->text() == QStringLiteral("Cursor")) cursor = b;
            QVERIFY(cursor && !cursor->isEnabled());
        }
        MediaDocument doc = videoDoc(dir.filePath(QStringLiteral("none.mp4")));
        CursorTrack track;
        track.videoSize = QSize(320, 180);
        track.samples = {{0, QPointF(280, 40), true}};
        doc.cursorTrack = track;
        EditorWindow w(doc, cfg, {});
        w.resize(900, 640);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        for (auto *b : w.findChildren<QToolButton *>(QStringLiteral("ZoomTarget")))
            if (b->text() == QStringLiteral("Cursor")) { QVERIFY(b->isEnabled()); b->click(); }
        QCOMPARE(w.studioDocument().zooms.first().target, ZoomSegment::Target::Cursor);
        QCOMPARE(undo->count(), steps + 1);
        // The selected Cursor zoom shows the window around the pointer, clamped in.
        const QRectF camera = w.findChild<Canvas *>()->camera();
        QCOMPARE(camera.size(), QSizeF(160, 90));
        QCOMPARE(camera.topLeft(), QPointF(160, 0));
    }
    void keepZoomedInFollowsThePointerUntilPlacedByHand() {
        QTemporaryDir dir;
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath(QStringLiteral("none.mp4")));
        CursorTrack track;
        track.videoSize = QSize(320, 180);
        track.samples = {{0, QPointF(300, 90), true}};
        doc.cursorTrack = track;
        EditorWindow w(doc, cfg, {});
        w.resize(900, 640);
        w.show();
        StudioDocument studio;
        studio.style = dusk();
        studio.style.aspect = QSize(9, 16);
        studio.keepZoomedIn = true;
        studio.keepCenter = QPointF(160, 90);
        w.setStudioDocument(studio);
        auto *canvas = w.findChild<Canvas *>();
        const QRectF base = canvas->contentRect();
        // The canvas shows the base view moved right, to the pointer.
        QVERIFY2(canvas->camera().center().x() > base.center().x() + 50,
                 qPrintable(QStringLiteral("%1 vs %2").arg(canvas->camera().center().x()).arg(base.center().x())));
        // Dragging the centre places it by hand; from then on it stays.
        emit canvas->cameraDragged(QPointF(10, 0));
        emit canvas->cameraDragFinished(false);
        QVERIFY(!w.studioDocument().keepFollowsCursor);
        QVERIFY(canvas->camera().isEmpty());
    }
    void suggestionsJoinThePopoverSession() {
        QTemporaryDir dir;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        Config cfg; cfg.animations = false;
        MediaDocument doc = videoDoc(dir.filePath(QStringLiteral("none.mp4")));
        CursorTrack track;
        track.videoSize = QSize(320, 180);
        for (qint64 ms = 0; ms < 4000; ms += 8)
            if (ms < 1000 || ms >= 2200 || ms == 1000) track.samples.append({ms, ms < 1000 || ms >= 2200 ? QPointF(10 + ms / 8, 20) : QPointF(200, 100), true});
        doc.cursorTrack = track;
        EditorWindow w(doc, cfg, cli);
        w.show();
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        w.openStudio();
        auto *popover = w.findChild<StudioPopover *>();
        auto *suggest = popover->findChild<QToolButton *>(QStringLiteral("StudioSuggest"));
        QVERIFY(suggest);
        suggest->click();
        QCOMPARE(w.studioDocument().zooms.size(), 1);
        QCOMPARE(w.studioDocument().zooms.first().point, QPointF(200, 100));
        popover->close();
        QTRY_VERIFY(!w.findChild<StudioPopover *>());
        QCOMPARE(undo->count(), steps + 1);
    }
    void aSavedPresetAppliesInOneStep() {
        QTemporaryDir dir;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        StudioPreset preset;
        preset.name = QStringLiteral("Warm");
        preset.style = dusk();
        preset.style.color = QColor(200, 90, 40);
        preset.motion = ZoomSegment::Motion::Smooth;
        QVERIFY(saveStudioPreset(cli.configPath, preset).ok);
        Config cfg; cfg.animations = false;
        EditorWindow w(videoDoc(dir.filePath(QStringLiteral("none.mp4"))), cfg, cli);
        w.show();
        QTest::keyClick(&w, Qt::Key_Z);   // a Focused zoom
        auto *undo = w.findChild<QUndoStack *>();
        const int steps = undo->count();
        w.openStudio();
        auto *popover = w.findChild<StudioPopover *>();
        auto *menu = popover->findChild<QMenu *>(QStringLiteral("StudioPresetMenu"));
        QVERIFY(menu);
        QCOMPARE(menu->actions().first()->text(), QStringLiteral("Warm"));
        menu->actions().first()->trigger();
        QCOMPARE(w.studioStyle(), preset.style);
        QCOMPARE(w.studioDocument().zooms.first().motion, ZoomSegment::Motion::Smooth);
        popover->close();
        QTRY_VERIFY(!w.findChild<StudioPopover *>());
        QCOMPARE(undo->count(), steps + 1);
    }
    void exportedZoomMatchesThePreview() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = markerClip(dir);
        QVERIFY(QFileInfo::exists(clip));
        Config cfg; cfg.animations = false; cfg.copyOnSave = false;
        CliOptions cli; cli.output.toFile = true; cli.output.filePath = dir.filePath(QStringLiteral("out.mp4"));
        EditorWindow w(videoDoc(clip), cfg, cli);
        w.resize(900, 640);
        w.show();
        QTRY_VERIFY(w.findChild<QMediaPlayer *>());
        auto *player = w.findChild<QMediaPlayer *>();
        player->audioOutput()->setMuted(true);
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        QTRY_VERIFY(player->isSeekable());
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        StudioDocument doc;
        doc.zooms = {{1, 1000, 3000, 2.0, ZoomSegment::Target::Point, QPointF(240, 60),
                      ZoomSegment::Motion::Focused}};
        w.setStudioDocument(doc);
        auto *canvas = w.findChild<Canvas *>();
        auto *timeline = w.findChild<VideoTimeline *>();
        // Where the marker sits in the content, as a fraction of the content's size.
        auto previewMarker = [&](qint64 ms) -> QPointF {
            player->pause();
            timeline->setPosition(ms);
            emit timeline->seekRequested(ms);
            if (!QTest::qWaitFor([&] { return qAbs(w.property("presentedStart").toLongLong() - ms) < 20; }, 5000))
                return QPointF(-3, w.property("presentedStart").toLongLong());
            QTest::qWait(50);
            const QImage shot = canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
            const QRectF camera = canvas->camera().isEmpty() ? QRectF(0, 0, 320, 180) : canvas->camera();
            const QRectF hole(canvas->mapFromScene(camera.topLeft()), canvas->mapFromScene(camera.bottomRight()));
            double sx = 0, sy = 0; int n = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c.red() > 180 && c.green() < 90 && c.blue() < 90) { sx += x; sy += y; ++n; }
                }
            return n ? QPointF((sx / n - hole.left()) / hole.width(), (sy / n - hole.top()) / hole.height())
                     : QPointF(-1, -1);
        };
        const QPointF before = previewMarker(500), during = previewMarker(1200), settled = previewMarker(2500);
        w.save();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(cli.output.filePath), 20000);
        auto exportMarker = [&](double seconds) -> QPointF {
            const QString frame = dir.filePath(QStringLiteral("frame-%1.png").arg(seconds));
            runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", QString::number(seconds),
                                                  "-i", cli.output.filePath, "-frames:v", "1", frame});
            const QImage image(frame);
            double sx = 0, sy = 0; int n = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 180 && c.green() < 90 && c.blue() < 90) { sx += x; sy += y; ++n; }
                }
            return n ? QPointF(sx / n / image.width(), sy / n / image.height()) : QPointF(-2, -2);
        };
        // Within about one output pixel of the 320x180 frame (studio plan 7.2).
        auto same = [](QPointF a, QPointF b) { return qAbs(a.x() - b.x()) * 320 <= 1.5 && qAbs(a.y() - b.y()) * 180 <= 1.5; };
        const QPointF exported[] = {exportMarker(0.5), exportMarker(1.2), exportMarker(2.5)};
        const QPointF previewed[] = {before, during, settled};
        for (int i = 0; i < 3; ++i)
            QVERIFY2(same(previewed[i], exported[i]),
                     qPrintable(QStringLiteral("%1: preview %2,%3 export %4,%5").arg(i)
                         .arg(previewed[i].x()).arg(previewed[i].y()).arg(exported[i].x()).arg(exported[i].y())));
        // The zoom really moved the marker: 2x around it puts it in the middle.
        QVERIFY(qAbs(settled.x() - 0.5) < 0.05 && qAbs(settled.y() - 0.5) < 0.05);
    }
};

QTEST_MAIN(TestStudioZooms)
#include "test_studiozooms.moc"

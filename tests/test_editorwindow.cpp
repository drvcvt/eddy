#include <QtTest>
#include "editorwindow.h"
#include "mediaio.h"
#include "redactbar.h"
#include "toast.h"
#include "dragpill.h"
#include "canvas.h"
#include "toolcontroller.h"
#include "items/rectitem.h"
#include "items/textitem.h"
#include "items/redactitem.h"
#include "textbar.h"
#include "spotlightbar.h"
#include "videotimeline.h"
#include "items/spotlightitem.h"
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QSlider>
#include <QToolButton>
#include <QLabel>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsVideoItem>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QSettings>
#include <QApplication>
#include <QUndoStack>
#include "theme.h"
using namespace eddy;

static bool have(const QString &cmd) {
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

static bool runProcess(const QString &program, const QStringList &args) {
    QProcess p;
    p.start(program, args);
    return p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

static bool sceneHasVideoItem(const EditorWindow &w) {
    const auto *scene = w.findChild<QGraphicsScene *>();
    if (!scene)
        return false;
    for (QGraphicsItem *item : scene->items()) {
        if (dynamic_cast<QGraphicsVideoItem *>(item))
            return true;
    }
    return false;
}

class TestEditorWindow : public QObject {
    Q_OBJECT
private slots:
    void professionalKeyboardGestures() {
        QImage bg(1000,500,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && canvas && undo);
        auto *item = new RectItem(QRectF(20,20,30,30));
        item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene->addItem(item);
        item->setSelected(true);

        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Right);
        QCOMPARE(item->pos(), QPointF(1,0));
        QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(item->pos(), QPointF(1,10));
        QCOMPARE(undo->count(), 2);

        QTest::keyClick(canvas, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(scene->selectedItems().size(), 1);
        QCOMPARE(scene->selectedItems().first()->pos(), QPointF(9,18));
        QCOMPARE(undo->count(), 3);

        QTest::keyPress(canvas, Qt::Key_Space);
        QVERIFY(canvas->spacePanActive());
        QTest::keyRelease(canvas, Qt::Key_Space);
        QVERIFY(!canvas->spacePanActive());

        QTest::keyClick(canvas, Qt::Key_Plus);
        QVERIFY(canvas->zoom() > 1.0);
        QTest::keyClick(canvas, Qt::Key_1);
        QCOMPARE(canvas->zoom(), 1.0);
    }
    void escapeCancelsActiveGestureBeforeClosing() {
        QImage bg(100,80,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *tools = w.findChild<ToolController *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && tools && undo);
        tools->setTool(ToolType::Rect);
        tools->begin({10,10});
        QCOMPARE(scene->items().size(), 2); // background + preview

        QTest::keyClick(&w, Qt::Key_Escape);
        QCOMPARE(scene->items().size(), 1);
        QCOMPARE(undo->count(), 0);
        QVERIFY(w.isVisible());
    }
    void deletingMultipleItemsIsOneUndoStep() {
        QImage bg(100,80,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && undo);
        for (int x : {10, 50}) {
            auto *item = new RectItem(QRectF(x,10,20,20));
            item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            scene->addItem(item);
            item->setSelected(true);
        }

        QTest::keyClick(&w, Qt::Key_Delete);
        QCOMPARE(scene->items().size(), 1);
        QCOMPARE(undo->count(), 1);
        undo->undo();
        QCOMPARE(scene->items().size(), 3);
    }
    void hasRedactBarAndToast() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.findChild<RedactBar *>() != nullptr);   // mode-bar constructed
        QVERIFY(w.findChild<Toast *>() != nullptr);        // toast constructed
        QVERIFY(w.findChild<RedactBar *>()->isHidden());   // hidden until a redact is selected
    }
    void selectedTextShowsContextBar() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *bar = w.findChild<TextBar *>();
        QVERIFY(scene && bar);
        QVERIFY(bar->isHidden());
        auto *text = new TextItem(QStringLiteral("Label"), Qt::red, 18);
        text->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(text);
        text->setSelected(true);
        QVERIFY(!bar->isHidden());
    }
    void textContextBarDoesNotCoverSelectedText() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        w.resize(w.minimumSize());
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *bar = w.findChild<TextBar *>();
        QVERIFY(scene && canvas && bar);
        auto *text = new TextItem(QStringLiteral("Move me"), Qt::red, 18);
        text->setPos(canvas->mapToScene(QPoint(160,2)));
        text->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(text);
        text->setSelected(true);
        QCoreApplication::processEvents();

        const QRect itemRect(
            canvas->mapFromScene(text->sceneBoundingRect().topLeft()),
            canvas->mapFromScene(text->sceneBoundingRect().bottomRight()));
        QVERIFY(!bar->geometry().intersects(itemRect.normalized()));
    }
    void textContextControlsAreSelfExplanatory() {
        TextBar bar;
        const QStringList names = {
            QStringLiteral("TextSize14"), QStringLiteral("TextSize20"), QStringLiteral("TextSize28"),
            QStringLiteral("TextBold"), QStringLiteral("TextAlignLeft"),
            QStringLiteral("TextAlignCenter"), QStringLiteral("TextAlignRight"),
            QStringLiteral("TextFill")};
        for (const QString &name : names) {
            auto *button = bar.findChild<QToolButton *>(name);
            QVERIFY2(button, qPrintable(name));
            QVERIFY2(!button->toolTip().isEmpty(), qPrintable(name));
            QVERIFY2(button->text().isEmpty(), qPrintable(name));
            QVERIFY2(!button->icon().isNull(), qPrintable(name));
            QCOMPARE(button->accessibleName(), button->toolTip());
        }
    }
    void spotlightContextChangesAreUndoable() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *bar = w.findChild<SpotlightBar *>();
        QVERIFY(scene && undo && bar);
        auto *spot = new SpotlightItem(QRectF(40,30,100,80), bg.size());
        spot->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(spot); spot->setSelected(true);
        QVERIFY(!bar->isHidden());

        emit bar->shapeChosen(SpotlightShape::Ellipse);
        QCOMPARE(spot->spotlightShape(), SpotlightShape::Ellipse);
        QCOMPARE(undo->count(), 1);
        undo->undo();
        QCOMPARE(spot->spotlightShape(), SpotlightShape::RoundedRect);

        emit bar->intensityChosen(3);
        QCOMPARE(spot->intensity(), 3);
        QCOMPARE(undo->count(), 1); // new edit replaces the abandoned redo branch
        undo->undo();
        QCOMPARE(spot->intensity(), 2);
    }
    void spotlightContextBarFollowsItemMove() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli); w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *bar = w.findChild<SpotlightBar *>();
        auto *spot = new SpotlightItem(QRectF(60,80,100,70), bg.size());
        spot->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(spot); spot->setSelected(true);
        QTest::qWait(20);
        const QPoint before = bar->pos();
        spot->moveBy(30, 15);
        QTest::qWait(20);
        QCOMPARE(bar->pos(), before + QPoint(30,15));
    }
    void redactContextBarFollowsItemMove() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli); w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *bar = w.findChild<RedactBar *>();
        auto *redact = new RedactItem(RedactMode::Blacken, bg, QRectF(60,80,100,70));
        redact->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(redact); redact->setSelected(true);
        QTest::qWait(20);
        const QPoint before = bar->pos();
        redact->moveBy(30, 15);
        QTest::qWait(20);
        QCOMPARE(bar->pos(), before + QPoint(30,15));
    }
    void themeButtonSwitchesAndPersists() {
        const QPalette oldPalette = QApplication::palette();
        const QString oldStyle = qApp->styleSheet();
        QApplication::setPalette(theme::palette(false));
        qApp->setStyleSheet(theme::styleSheet(false));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Config cfg; cfg.animations = false; cfg.theme = ThemeMode::Light;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        QImage bg(100,80,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        EditorWindow w(bg, cfg, cli);
        auto *button = w.findChild<QToolButton *>(QStringLiteral("Theme"));
        QVERIFY(button);

        button->click();
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor("#121212"));
        QSettings settings(cli.configPath, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("eddy/theme")).toString(), QStringLiteral("dark"));
        button->click();
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor("#FAFAFA"));

        QApplication::setPalette(oldPalette);
        qApp->setStyleSheet(oldStyle);
    }
    void textKeyboardCommitAndCancelUseSingleTransaction() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli); w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *tools = w.findChild<ToolController *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && canvas && tools && undo);

        auto *committed = dynamic_cast<TextItem *>(tools->placeText({30,30}));
        QVERIFY(committed);
        committed->setPlainText(QStringLiteral("Two\nlines"));
        QTest::keyClick(canvas, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(tools->editingText(), nullptr);
        QCOMPARE(undo->count(), 1);
        QVERIFY(committed->scene() == scene);

        auto *cancelled = dynamic_cast<TextItem *>(tools->placeText({60,60}));
        QVERIFY(cancelled);
        cancelled->setPlainText(QStringLiteral("Discard me"));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QCOMPARE(tools->editingText(), nullptr);
        QCOMPARE(undo->count(), 1);
        int textCount = 0;
        for (QGraphicsItem *item : scene->items())
            if (dynamic_cast<TextItem *>(item)) ++textCount;
        QCOMPARE(textCount, 1);
    }
    void buildsAndExportsCompositeSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QImage out = w.exportComposite();
        QCOMPARE(out.size(), QSize(64,48));
    }
    void exportKeepsAnnotationSelected() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        QVERIFY(scene);
        auto *item = scene->addRect(QRectF(4, 4, 12, 12));
        item->setFlag(QGraphicsItem::ItemIsSelectable);
        item->setSelected(true);

        w.exportComposite();

        QVERIFY(item->isSelected());
    }
    void hasMinimumSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.minimumWidth() > 0);
        QVERIFY(w.minimumHeight() > 0);
        QVERIFY(w.minimumWidth() <= 520);
    }
    void hasDragOutPill() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *pill = w.findChild<DragPill *>();
        QVERIFY(pill != nullptr);            // pill exists and is visible (not hidden like the bar/toast)
        QVERIFY(!pill->isHidden());
    }
    void hasSendToShelfAction() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *button = w.findChild<QToolButton *>(QStringLiteral("SendToShelf"));
        QVERIFY(button != nullptr);
        QVERIFY(!button->isHidden());
    }
    void saveRoutingUsesExplicitCardConfigShelfPriority() {
        CliOptions cli;
        Config cfg;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::Shelf);

        cfg.saveDir = QStringLiteral("/tmp/configured");
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ConfigDirectory);

        cli.boltsnapCardId = 42;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::BoltsnapCard);

        cli.output.toFile = true;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ExplicitOutput);

        cli.output.toFile = false;
        cli.output.toStdout = true;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ExplicitOutput);

        cli.output.toStdout = false;
        cli.output.saveDir = QStringLiteral("/tmp/eddy-test");
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ExplicitOutput);
    }
    void configuredSaveDirectoryWritesImage() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        Config cfg;
        cfg.saveDir = dir.path();
        cfg.copyOnSave = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);

        w.save();

        const QStringList files = QDir(dir.path()).entryList(
            {QStringLiteral("eddy-*.png")}, QDir::Files);
        QCOMPARE(files.size(), 1);
    }
    void videoDocumentBuildsPlaybackControlsAndTransparentOverlay() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *bar = w.findChild<QWidget *>(QStringLiteral("PlaybackBar"));
        QVERIFY(bar != nullptr);
        QVERIFY(bar->testAttribute(Qt::WA_StyledBackground));
        auto *play = w.findChild<QToolButton *>(QStringLiteral("PlaybackPlay"));
        QVERIFY(play != nullptr);
        QVERIFY(play->text().isEmpty());
        QVERIFY(!play->icon().isNull());
        QVERIFY(!play->toolTip().isEmpty());
        QCOMPARE(play->accessibleName(), play->toolTip());
        QVERIFY(w.findChild<VideoTimeline *>(QStringLiteral("VideoTimeline")) != nullptr);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("TrimSetIn")) != nullptr);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("TrimSetOut")) != nullptr);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("TrimReset")) != nullptr);
        auto *mute = w.findChild<QToolButton *>(QStringLiteral("PlaybackMute"));
        QVERIFY(mute != nullptr);
        QVERIFY(mute->text().isEmpty());
        QVERIFY(!mute->icon().isNull());
        QCOMPARE(mute->accessibleName(), mute->toolTip());
        auto *reset = w.findChild<QToolButton *>(QStringLiteral("TrimReset"));
        QVERIFY(reset != nullptr);
        QVERIFY(reset->text().isEmpty());
        QVERIFY(!reset->icon().isNull());
        auto *volume = w.findChild<QSlider *>(QStringLiteral("PlaybackVolume"));
        QVERIFY(volume != nullptr);
        QCOMPARE(volume->value(), 100);
        QVERIFY(w.findChild<DragPill *>() != nullptr);
        QImage overlay = w.exportComposite();
        QCOMPARE(overlay.size(), QSize(64, 48));
        QCOMPARE(overlay.pixelColor(10, 10).alpha(), 0);
    }
    void videoTrimKeyboardControlsUpdateTheRange() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        doc.video.fps = 25.0;
        Config cfg;
        cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *timeline = w.findChild<VideoTimeline *>();
        QVERIFY(timeline != nullptr);

        timeline->setPosition(200);
        QTest::keyClick(&w, Qt::Key_I);
        QCOMPARE(timeline->trimIn(), 200);
        timeline->setPosition(800);
        QTest::keyClick(&w, Qt::Key_O);
        QCOMPARE(timeline->trimOut(), 800);
        QTest::mouseClick(w.findChild<QToolButton *>(QStringLiteral("TrimReset")), Qt::LeftButton);
        QCOMPARE(timeline->trimIn(), 0);
        QCOMPARE(timeline->trimOut(), 1000);
    }
    void videoTrimIsUndoableAndShowsPreciseRange() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 12500;
        doc.video.fps = 25.0;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *inTime = w.findChild<QLabel *>(QStringLiteral("TrimInTime"));
        auto *outTime = w.findChild<QLabel *>(QStringLiteral("TrimOutTime"));
        QVERIFY(timeline && undo && inTime && outTime);
        QCOMPARE(inTime->text(), QStringLiteral("0:00.000"));
        QCOMPARE(outTime->text(), QStringLiteral("0:12.500"));

        timeline->setPosition(1234);
        QTest::keyClick(&w, Qt::Key_I);
        QCOMPARE(timeline->trimIn(), 1234);
        QCOMPARE(inTime->text(), QStringLiteral("0:01.234"));
        QCOMPARE(undo->count(), 1);

        undo->undo();
        QCOMPARE(timeline->trimIn(), 0);
        QCOMPARE(inTime->text(), QStringLiteral("0:00.000"));
        undo->redo();
        QCOMPARE(timeline->trimIn(), 1234);
        QCOMPARE(inTime->text(), QStringLiteral("0:01.234"));
    }
    void failedRequestedVideoExportIsVisible() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/eddy-missing-video-export-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        doc.video.fps = 25.0;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *toast = w.findChild<Toast *>();
        QVERIFY(timeline && toast);
        timeline->setPosition(200);
        QTest::keyClick(&w, Qt::Key_I);

        w.sendToShelf();
        QVERIFY(toast->text().contains(QStringLiteral("Preparing")));
        QTRY_COMPARE_WITH_TIMEOUT(toast->text(), QStringLiteral("Video export failed"), 5000);
    }
    void videoDocumentDefersPlayerCreationUntilShow() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        QVERIFY(w.findChild<QMediaPlayer *>() == nullptr);
        w.show();
        QTest::qWait(120);
        auto *player = w.findChild<QMediaPlayer *>();
        QVERIFY(player != nullptr);
        QVERIFY(player->audioOutput() != nullptr);
        QCOMPARE(player->audioOutput()->volume(), 1.0f);
        QCOMPARE(player->source(), QUrl::fromLocalFile(doc.path));
    }
    void videoDocumentDefersVideoSurfaceUntilPlaybackLoad() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        QVERIFY(!sceneHasVideoItem(w));

        w.show();
        QTest::qWait(120);
        QVERIFY(sceneHasVideoItem(w));
    }
    void videoCopyWithoutAnnotationsUsesSourceUrlOnly() {
        QTemporaryFile f("XXXXXX.mp4");
        QVERIFY(f.open());
        const QByteArray bytes("not a real video, but copy should not export clean video scenes");
        QCOMPARE(f.write(bytes), bytes.size());
        f.flush();

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QFileInfo(f.fileName()).canonicalFilePath();
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);

        w.copy();
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasUrls());
        QVERIFY(!mime->hasFormat("video/mp4"));
        QCOMPARE(mime->urls().size(), 1);
        QCOMPARE(mime->urls().first().toLocalFile(), doc.path);
    }
    void dirtyVideoCopyDoesNotBlockWhenCachedExportIsMissing() {
        if (!have(QStringLiteral("ffmpeg")))
            QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=1:r=5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            video
        }));

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QFileInfo(video).canonicalFilePath();
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        QVERIFY(scene != nullptr);
        scene->addItem(new RectItem(QRectF(4, 4, 12, 12)));

        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
        QElapsedTimer timer;
        timer.start();
        w.copy();

        QVERIFY2(timer.elapsed() < 200,
                 qPrintable(QStringLiteral("copy blocked for %1ms").arg(timer.elapsed())));
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("sentinel"));
    }
};
QTEST_MAIN(TestEditorWindow)
#include "test_editorwindow.moc"

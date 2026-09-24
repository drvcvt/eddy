#include <QtTest>
#include <QGraphicsScene>
#include <QProcess>
#include <QStandardPaths>
#include <QToolButton>
#include <QUndoStack>
#include "editorwindow.h"
#include "projectstore.h"
#include "recoverystore.h"
#include "toast.h"
#include "undocommands.h"
#include "videotimeline.h"
#include "toolcontroller.h"
#include "items/rectitem.h"
#include "items/redactitem.h"
#include "items/textitem.h"

using namespace eddy;

static bool have(const QString &cmd) { return !QStandardPaths::findExecutable(cmd).isEmpty(); }

static QImage pattern() {
    QImage image(320, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(30, 60, 120));
    for (int x = 0; x < 320; x += 16)
        for (int y = 0; y < 200; ++y) image.setPixelColor(x, y, Qt::white);
    return image;
}

static void annotate(EditorWindow &w) {
    auto *scene = w.findChild<QGraphicsScene *>();
    auto *rect = new RectItem(QRectF(20, 20, 100, 60));
    rect->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    scene->addItem(rect);
    auto *text = new TextItem(QStringLiteral("Hello"), Qt::yellow, 20);
    text->setPos(150, 30);
    text->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    scene->addItem(text);
    auto *blur = new RedactItem(RedactMode::Blur, pattern(), QRectF(40, 120, 90, 50));
    blur->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    scene->addItem(blur);
}

static bool saveAndWait(EditorWindow &w, const QString &path, bool saveAs = false) {
    QSignalSpy saved(&w, &EditorWindow::projectSaved);
    w.saveProject(saveAs, path);
    return saved.wait(20000) && QFileInfo::exists(path);
}

class TestProjects : public QObject {
    Q_OBJECT
private slots:
    void anImageProjectReopensAsItWasLeft() {
        QTemporaryDir dir;
        const QString source = dir.filePath("shot.png");
        QVERIFY(pattern().save(source));
        MediaDocument doc = loadMediaInput({InputSpec::File, source}).document;
        Config cfg; cfg.animations = false;
        EditorWindow w(doc, cfg, {});
        annotate(w);
        StudioStyle style;
        style.background = StudioStyle::Background::Color;
        style.color = QColor(200, 80, 40);
        w.setStudioStyle(style);
        const QImage before = w.exportComposite();
        const QString project = dir.filePath("demo.eddy");
        QVERIFY(saveAndWait(w, project));
        QCOMPARE(w.projectPath(), project);
        // The original stays; the project copied it.
        QVERIFY(QFileInfo::exists(source));
        QString error;
        std::unique_ptr<EditorWindow> reopened(openProjectWindow(project, cfg, {}, &error));
        QVERIFY2(reopened, qPrintable(error));
        QCOMPARE(reopened->exportComposite(), before);
        QCOMPARE(reopened->studioStyle(), style);
        QCOMPARE(reopened->findChild<QUndoStack *>()->count(), 0);
        // Saving again without a dialog, then as a copy with its own original.
        QVERIFY(saveAndWait(*reopened, project));
        // Saving a reopened project again does not copy its original again:
        // with the assets folder read-only it still succeeds.
        QFile::setPermissions(projectAssetsDir(project), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        QVERIFY(saveAndWait(*reopened, project));
        QFile::setPermissions(projectAssetsDir(project), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QString copy = dir.filePath("copy.eddy");
        QVERIFY(saveAndWait(*reopened, copy, true));
        const OpenedProject opened = openProject(copy);
        QVERIFY2(opened.ok, qPrintable(opened.error));
        QVERIFY(opened.sourcePath.startsWith(projectAssetsDir(copy)));
    }
    void aStdinImageIsKeptAsPng() {
        QTemporaryDir dir;
        MediaDocument doc;
        doc.kind = MediaKind::Image;
        doc.image = pattern();
        Config cfg; cfg.animations = false;
        EditorWindow w(doc, cfg, {});
        const QString project = dir.filePath("pasted.eddy");
        QVERIFY(saveAndWait(w, project));
        const OpenedProject opened = openProject(project);
        QVERIFY2(opened.ok, qPrintable(opened.error));
        QVERIFY(opened.sourcePath.endsWith(".png"));
        QCOMPARE(QImage(opened.sourcePath).convertToFormat(QImage::Format_ARGB32_Premultiplied), pattern());
    }
    void aVideoProjectKeepsTrimZoomsAndExport() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString clip = dir.filePath("clip.mp4");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=160x90:r=25:d=3", "-pix_fmt", "yuv420p", clip});
        QVERIFY(ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0);
        const LoadMediaResult media = loadMediaInput({InputSpec::File, clip});
        QVERIFY2(media.ok, qPrintable(media.error));
        Config cfg; cfg.animations = false;
        CliOptions cli; cli.configPath = dir.filePath("config");
        EditorWindow w(media.document, cfg, cli);
        StudioDocument studio;
        studio.zooms = {{1, 500, 1500, 2.0, ZoomSegment::Target::Point, QPointF(40, 30), ZoomSegment::Motion::Smooth}};
        w.setStudioDocument(studio);
        auto *timeline = w.findChild<VideoTimeline *>();
        timeline->setTrimRange(400, 2600);
        emit timeline->trimCommitted(400, 2600);
        const QString project = dir.filePath("clip.eddy");
        QVERIFY(saveAndWait(w, project));
        QString error;
        std::unique_ptr<EditorWindow> reopened(openProjectWindow(project, cfg, cli, &error));
        QVERIFY2(reopened, qPrintable(error));
        QVERIFY(reopened->studioDocument() == studio);
        auto *reTimeline = reopened->findChild<VideoTimeline *>();
        QCOMPARE(reTimeline->trimIn(), 400);
        QCOMPARE(reTimeline->trimOut(), 2600);
    }
    void normalOutputNeverWritesIntoAProject() {
        QTemporaryDir dir;
        const QString source = dir.filePath("shot.png");
        QVERIFY(pattern().save(source));
        Config cfg; cfg.animations = false; cfg.copyOnSave = false;
        const QString project = dir.filePath("demo.eddy");
        {
            EditorWindow w(loadMediaInput({InputSpec::File, source}).document, cfg, {});
            QVERIFY(saveAndWait(w, project));
        }
        const OpenedProject opened = openProject(project);
        const QByteArray bytes = [&] { QFile f(opened.sourcePath); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        CliOptions cli; cli.output.toFile = true; cli.output.filePath = opened.sourcePath;
        EditorWindow w(loadMediaInput({InputSpec::File, source}).document, cfg, cli);
        annotate(w);
        w.save();
        QFile f(opened.sourcePath);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), bytes);
    }
    // Kept edits: on for these tests only, in a folder of their own.
    void keptEditsFollowFinishedChangesOnly() {
        QTemporaryDir dir, recovery;
        qputenv("EDDY_RECOVERY_DIR", recovery.path().toLocal8Bit());
        EditorWindow::setRecoveryEnabledByDefault(true);
        const auto off = qScopeGuard([] { EditorWindow::setRecoveryEnabledByDefault(false); qunsetenv("EDDY_RECOVERY_DIR"); });
        const QString source = dir.filePath("shot.png");
        QVERIFY(pattern().save(source));
        Config cfg; cfg.animations = false;
        auto w = std::make_unique<EditorWindow>(loadMediaInput({InputSpec::File, source}).document, cfg, CliOptions{});
        w->setRecoveryDelays(50, 400);
        auto *scene = w->findChild<QGraphicsScene *>();
        auto *undo = w->findChild<QUndoStack *>();
        QSignalSpy written(w.get(), &EditorWindow::recoveryWritten);
        // Nothing is kept while a text is being typed.
        w->findChild<ToolController *>()->placeText(QPointF(10, 10));
        auto *rect = new RectItem(QRectF(20, 20, 60, 40));
        undo->push(new AddItemCommand(scene, rect));
        QVERIFY(!written.wait(300));
        w->findChild<ToolController *>()->cancelTextEdit();
        QVERIFY(written.wait(3000));
        const QString manifest = w->recoveryManifest();
        OpenedProject kept = openProject(manifest);
        QVERIFY2(kept.ok, qPrintable(kept.error));
        QCOMPARE(kept.snapshot.items.size(), 1);
        const auto entries = RecoveryStore().entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries[0].source, source);
        QVERIFY(entries[0].inUse);                         // this window holds it
        // Closing keeps the very last change at once.
        w->setRecoveryDelays(60000, 60000);
        undo->push(new AddItemCommand(scene, new RectItem(QRectF(100, 100, 30, 30))));
        w->close();
        w.reset();
        kept = openProject(manifest);
        QCOMPARE(kept.snapshot.items.size(), 2);
        QVERIFY(!RecoveryStore().entries()[0].inUse);
        // Reopening the same file offers the kept edit.
        EditorWindow again(loadMediaInput({InputSpec::File, source}).document, cfg, CliOptions{});
        again.show();
        QTRY_VERIFY(again.findChild<Toast *>()->text().contains(QStringLiteral("kept edit")));
        // Resuming continues the same entry as an unnamed edit.
        QString error;
        std::unique_ptr<EditorWindow> resumed(openProjectWindow(manifest, cfg, {}, &error));
        QVERIFY2(resumed, qPrintable(error));
        resumed->adoptRecovery(entries[0].id);
        QVERIFY(resumed->projectPath().isEmpty());
        QCOMPARE(resumed->recoveryManifest(), manifest);
        resumed->setRecoveryDelays(50, 400);
        QSignalSpy resumedWritten(resumed.get(), &EditorWindow::recoveryWritten);
        resumed->findChild<QUndoStack *>()->push(new AddItemCommand(resumed->findChild<QGraphicsScene *>(),
                                                                    new RectItem(QRectF(5, 5, 10, 10))));
        QVERIFY(resumedWritten.wait(3000));
        QCOMPARE(resumedWritten.first().first().toString(), manifest);
        QCOMPARE(RecoveryStore().entries().size(), 1);
        QCOMPARE(openProject(manifest).snapshot.items.size(), 3);
    }
    void aPlainToastDropsTheOffer() {
        QWidget parent;
        Toast toast(&parent);
        int runs = 0;
        toast.showAction(QStringLiteral("A kept edit is newer"), QStringLiteral("Resume"), [&] { ++runs; });
        auto *action = toast.findChild<QToolButton *>(QStringLiteral("ToastAction"));
        QVERIFY(!action->isHidden());
        toast.showMessage(QStringLiteral("Preset saved"));
        QVERIFY(action->isHidden());
        action->click();
        QCOMPARE(runs, 0);
    }
    void closingWaitsForTheRunningSnapshot() {
        QTemporaryDir dir, recovery;
        qputenv("EDDY_RECOVERY_DIR", recovery.path().toLocal8Bit());
        EditorWindow::setRecoveryEnabledByDefault(true);
        const auto off = qScopeGuard([] { EditorWindow::setRecoveryEnabledByDefault(false); qunsetenv("EDDY_RECOVERY_DIR"); });
        // Noise does not compress: a large file makes the first copy take a while.
        QImage noise(1600, 1600, QImage::Format_RGB32);
        QRandomGenerator random(7);
        for (int y = 0; y < noise.height(); ++y)
            random.fillRange(reinterpret_cast<quint32 *>(noise.scanLine(y)), noise.width());
        const QString source = dir.filePath("noise.png");
        QVERIFY(noise.save(source, nullptr, 0));
        Config cfg; cfg.animations = false;
        auto w = std::make_unique<EditorWindow>(loadMediaInput({InputSpec::File, source}).document, cfg, CliOptions{});
        w->setRecoveryDelays(1, 60000);
        auto *scene = w->findChild<QGraphicsScene *>();
        auto *undo = w->findChild<QUndoStack *>();
        undo->push(new AddItemCommand(scene, new RectItem(QRectF(20, 20, 60, 40))));
        // Stop as soon as the first snapshot has started, before it can report back.
        QElapsedTimer started;
        started.start();
        while (w->recoveryManifest().isEmpty() && started.elapsed() < 5000) QCoreApplication::processEvents();
        QVERIFY(!w->recoveryManifest().isEmpty());
        undo->push(new AddItemCommand(scene, new RectItem(QRectF(100, 100, 30, 30))));
        const QString manifest = w->recoveryManifest();
        QSignalSpy written(w.get(), &EditorWindow::recoveryWritten);
        QVERIFY(!w->close());   // not while the original is still being copied
        QVERIFY(written.wait(20000));
        // Then it closes by itself, with the last change kept as well.
        QCOMPARE(openProject(manifest).snapshot.items.size(), 2);
        QCOMPARE(RecoveryStore().entries().size(), 1);
    }
};

QTEST_MAIN(TestProjects)
#include "test_projects.moc"

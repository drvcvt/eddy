#include <QtTest>
#include <QGraphicsScene>
#include <QProcess>
#include <QStandardPaths>
#include <QUndoStack>
#include "editorwindow.h"
#include "projectstore.h"
#include "videotimeline.h"
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
};

QTEST_MAIN(TestProjects)
#include "test_projects.moc"

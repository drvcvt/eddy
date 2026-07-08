#include <QtTest>
#include "editorwindow.h"
#include "mediaio.h"
#include "redactbar.h"
#include "toast.h"
#include "dragpill.h"
#include <QMediaPlayer>
#include <QSlider>
#include <QToolButton>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsVideoItem>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QUndoStack>
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
    void hasRedactBarAndToast() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.findChild<RedactBar *>() != nullptr);   // mode-bar constructed
        QVERIFY(w.findChild<Toast *>() != nullptr);        // toast constructed
        QVERIFY(w.findChild<RedactBar *>()->isHidden());   // hidden until a redact is selected
    }
    void buildsAndExportsCompositeSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QImage out = w.exportComposite();
        QCOMPARE(out.size(), QSize(64,48));
    }
    void hasMinimumSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.minimumWidth() > 0);
        QVERIFY(w.minimumHeight() > 0);
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
    void imageSaveRoutingPrefersShelfOnlyWithoutExplicitOutput() {
        CliOptions cli;
        QVERIFY(imageSaveUsesShelfReturn(cli));

        cli.output.toFile = true;
        QVERIFY(!imageSaveUsesShelfReturn(cli));

        cli.output.toFile = false;
        cli.output.toStdout = true;
        QVERIFY(!imageSaveUsesShelfReturn(cli));

        cli.output.toStdout = false;
        cli.output.saveDir = QStringLiteral("/tmp/eddy-test");
        QVERIFY(!imageSaveUsesShelfReturn(cli));
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
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("PlaybackPlay")) != nullptr);
        QVERIFY(w.findChild<QSlider *>(QStringLiteral("PlaybackPosition")) != nullptr);
        QVERIFY(w.findChild<DragPill *>() != nullptr);
        QImage overlay = w.exportComposite();
        QCOMPARE(overlay.size(), QSize(64, 48));
        QCOMPARE(overlay.pixelColor(10, 10).alpha(), 0);
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
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(undo != nullptr);
        undo->push(new QUndoCommand(QStringLiteral("mark dirty")));

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

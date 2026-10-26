#include "editortest.h"

class TestEditorVideoOut : public QObject {
    Q_OBJECT
private slots:
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
    void dirtyVideoCopyCompletesAsynchronously() {
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
        QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->mimeData()->hasUrls(), 5000);
        const QString cached = QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile();
        QVERIFY(QFileInfo::exists(cached));
        QVERIFY(cached != doc.path);
    }
    void trimmedVideoCopySurvivesWindowClose() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=2:r=25"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video
        }));

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QFileInfo(video).canonicalFilePath();
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 2000;
        doc.video.fps = 25.0;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        QString copied;
        {
            EditorWindow w(doc, cfg, cli);
            auto *timeline = w.findChild<VideoTimeline *>();
            QVERIFY(timeline);
            timeline->setPosition(500);
            QTest::keyClick(&w, Qt::Key_I);
            QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
            w.copy();
            QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->mimeData()->hasUrls(), 5000);
            copied = QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile();
            QVERIFY(copied != doc.path);
            timeline->setPosition(800);
            QTest::keyClick(&w, Qt::Key_I);
            QTest::mouseClick(w.findChild<DragPill *>(), Qt::LeftButton);
            QTRY_COMPARE_WITH_TIMEOUT(w.findChild<QLabel *>("DragPillText")->text(),
                                      QStringLiteral("Drag out"), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(w.findChild<DragPill *>()->isEnabled(), 5000);
            QVERIFY2(QFileInfo::exists(copied),
                     "cache refresh removed the video still referenced by the clipboard");
        }

        QVERIFY(QFileInfo::exists(copied));
        const auto probe = probeVideoFile(copied);
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QVERIFY2(qAbs(probe.info.durationMs - 1500) <= 80,
                 qPrintable(QStringLiteral("duration was %1 ms").arg(probe.info.durationMs)));
        QVERIFY(QFile::remove(copied));
    }
    void replacedClipboardReleasesCachedVideoOnClose() {
        if (!have(QStringLiteral("ffmpeg")))
            QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=1:r=5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video
        }));
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = video;
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        QString cached;
        {
            EditorWindow w(doc, cfg, cli);
            w.findChild<QGraphicsScene *>()->addItem(new RectItem(QRectF(4, 4, 12, 12)));
            QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
            w.copy();
            QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->mimeData()->hasUrls(), 5000);
            cached = QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile();
            QGuiApplication::clipboard()->setText(QStringLiteral("external replacement"));
        }
        QVERIFY2(!QFileInfo::exists(cached), "obsolete clipboard cache was leaked");
    }
    void fileSaveReusesReadyVideoCache() {
        if (!have(QStringLiteral("ffmpeg")))
            QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("saved.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=1:r=5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video
        }));

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QFileInfo(video).canonicalFilePath();
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false; cfg.copyOnSave = false;
        CliOptions cli; cli.output.toFile = true; cli.output.filePath = output;
        QByteArray expected;
        {
            EditorWindow w(doc, cfg, cli);
            w.findChild<QGraphicsScene *>()->addItem(new RectItem(QRectF(4, 4, 12, 12)));

            QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
            w.copy();
            QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->mimeData()->hasUrls(), 5000);
            const QString cached = QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile();
            QVERIFY(QFileInfo::exists(cached));
            QVERIFY(QFile::remove(video));

            w.save();
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 5000);
            QFile cachedFile(cached);
            QVERIFY(cachedFile.open(QIODevice::ReadOnly));
            expected = cachedFile.readAll();
        }
        QFile savedFile(output);
        QVERIFY(savedFile.open(QIODevice::ReadOnly));
        QCOMPARE(savedFile.readAll(), expected);
    }
    void uneditedVideoSaveCreatesIndependentFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("saved.mp4"));
        QFile source(input);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("original-video"), qint64(14));
        source.close();

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = input;
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false;
        CliOptions cli; cli.output.toFile = true; cli.output.filePath = output;
        EditorWindow w(doc, cfg, cli);
        w.save();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 5000);

        QVERIFY(source.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(source.write("changed"), qint64(7));
        source.close();
        QFile saved(output);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("original-video"));
    }
    void closeWaitsForVideoSave() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        const QString output = dir.filePath(QStringLiteral("saved.mp4"));
        QFile source(input);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(QByteArray(1024 * 1024, 'v')), qint64(1024 * 1024));
        source.close();

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = input;
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false;
        CliOptions cli; cli.output.toFile = true; cli.output.filePath = output;
        EditorWindow w(doc, cfg, cli);
        w.show();
        w.save();
        QVERIFY(!w.close());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(output), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(w.close(), 5000);
    }
    void closeWaitsForPendingVideoCopy() {
        if (!have(QStringLiteral("ffmpeg")))
            QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=640x360:d=1:r=30"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video
        }));
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = video;
        doc.video.size = QSize(640, 360);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        w.show();
        w.findChild<QGraphicsScene *>()->addItem(new RectItem(QRectF(4, 4, 12, 12)));
        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));
        w.copy();

        QVERIFY2(!w.close(), "window closed before the requested video copy completed");
        QTRY_VERIFY_WITH_TIMEOUT(QGuiApplication::clipboard()->mimeData()->hasUrls(), 5000);
        QVERIFY(QFileInfo::exists(
            QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile()));
    }
#ifndef Q_OS_WIN
    void videoShelfAcknowledgementDoesNotBlockUi() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QFile source(video);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("video"), qint64(5));
        source.close();
        const QString socketPath = dir.filePath(QStringLiteral("boltsnap.sock"));
        const QByteArray socketBytes = QFile::encodeName(socketPath);
        const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        QVERIFY(server >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, socketBytes.constData(), size_t(socketBytes.size()));
        QVERIFY2(::bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0,
                 std::strerror(errno));
        QVERIFY2(::listen(server, 1) == 0, std::strerror(errno));

        const QByteArray response = videoResponseFrame(video);
        std::thread responder([server, response] {
            pollfd ready{server, POLLIN, 0};
            const int client = ::poll(&ready, 1, 2000) > 0
                ? ::accept(server, nullptr, nullptr) : -1;
            if (client >= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                ::send(client, response.constData(), size_t(response.size()), MSG_NOSIGNAL);
                ::close(client);
            }
        });
        qputenv("EDDY_BOLTSNAP_SOCKET", socketBytes);
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = video;
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *toast = w.findChild<Toast *>();
        QElapsedTimer callTimer;
        callTimer.start();
        w.sendToShelf();
        const qint64 callMs = callTimer.elapsed();
        QElapsedTimer completionTimer;
        completionTimer.start();
        while (completionTimer.elapsed() < 2000
               && toast->text() != QStringLiteral("Sent to Boltsnap shelf")) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QTest::qWait(10);
        }
        responder.join();
        ::close(server);
        qunsetenv("EDDY_BOLTSNAP_SOCKET");

        QVERIFY2(callMs < 200,
                 qPrintable(QStringLiteral("sendToShelf blocked for %1 ms").arg(callMs)));
        QCOMPARE(toast->text(), QStringLiteral("Sent to Boltsnap shelf"));
        QVERIFY2(QFileInfo::exists(video), "handoff removed the original video");
    }
#endif
    void failedVideoSaveToShelfFallsBackToTheClipboard() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QFile source(video);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write("video"), qint64(5));
        source.close();

        const QByteArray oldSocket = qgetenv("EDDY_BOLTSNAP_SOCKET");
        qputenv("EDDY_BOLTSNAP_SOCKET",
                QFile::encodeName(dir.filePath(QStringLiteral("missing-boltsnap"))));
        QGuiApplication::clipboard()->setText(QStringLiteral("sentinel"));

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = video;
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        cfg.copyOnSave = false;
        EditorWindow window(doc, cfg, {});
        auto *toast = window.findChild<Toast *>();

        window.save();
        QTRY_COMPARE_WITH_TIMEOUT(toast->text(),
                                  QStringLiteral("Boltsnap shelf unavailable"), 3000);
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasUrls());
        QCOMPARE(QFileInfo(QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile())
                     .canonicalFilePath(),
                 QFileInfo(video).canonicalFilePath());

        if (oldSocket.isNull())
            qunsetenv("EDDY_BOLTSNAP_SOCKET");
        else
            qputenv("EDDY_BOLTSNAP_SOCKET", oldSocket);
    }
    void videoEditsOnlyExportWhenRequested() {
        if (!have(QStringLiteral("ffmpeg")))
            QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString video = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
            QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x48:d=1:r=5"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), video
        }));
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = video;
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *pill = w.findChild<DragPill *>();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(pill && scene && undo);

        undo->push(new AddItemCommand(scene, new RectItem(QRectF(4, 4, 12, 12))));
        auto *timer = w.findChild<QTimer *>("VideoExportTimer");
        auto *label = pill->findChild<QLabel *>("DragPillText");
        QVERIFY(timer && label);
        QVERIFY(!timer->isActive());
        QVERIFY(pill->isEnabled());
        QCOMPARE(label->text(), QStringLiteral("Prepare drag"));
        QTest::qWait(450); // Editing alone must not start the old debounced export.
        QCOMPARE(label->text(), QStringLiteral("Prepare drag"));
        QVERIFY(!timer->isActive());

        QTest::mouseClick(pill, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(label->text(), QStringLiteral("Drag out"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pill->isEnabled(), 5000);
        undo->undo();
        QCOMPARE(label->text(), QStringLiteral("Drag out"));
        QVERIFY(!timer->isActive());
        undo->redo();
        QCOMPARE(label->text(), QStringLiteral("Prepare drag"));
        QVERIFY(!timer->isActive());
        QTest::keyClick(pill, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(label->text(), QStringLiteral("Drag out"), 5000);
    }
};
QTEST_MAIN(TestEditorVideoOut)
#include "test_editorvideoout.moc"

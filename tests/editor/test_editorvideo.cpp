#include "editortest.h"
#include "studio/studiodocument.h"

class TestEditorVideo : public QObject {
    Q_OBJECT
private slots:
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
        QCOMPARE(play->accessibleName(), theme::spokenName(play->toolTip()));
        QVERIFY(!play->accessibleName().contains(QLatin1Char('\t')));
        QVERIFY(w.findChild<VideoTimeline *>(QStringLiteral("VideoTimeline")) != nullptr);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("TrimSetIn")) != nullptr);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("TrimSetOut")) != nullptr);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("TrimReset")) != nullptr);
        auto *mute = w.findChild<QToolButton *>(QStringLiteral("PlaybackMute"));
        QVERIFY(mute != nullptr);
        QVERIFY(mute->text().isEmpty());
        QVERIFY(!mute->icon().isNull());
        QCOMPARE(mute->accessibleName(), theme::spokenName(mute->toolTip()));
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
    void currentVideoFrameFeedsExistingAndNewBlurItems() {
        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-video-frame-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        Config cfg;
        cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        auto *scene = window.findChild<QGraphicsScene *>();
        auto *tools = window.findChild<ToolController *>();
        QVERIFY(scene && tools);

        tools->setTool(ToolType::Redact);
        tools->begin({4, 4});
        tools->finish({24, 24});

        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(sceneHasVideoItem(window), 1000);
        QGraphicsVideoItem *videoItem = nullptr;
        for (QGraphicsItem *item : scene->items())
            if ((videoItem = dynamic_cast<QGraphicsVideoItem *>(item))) break;
        QVERIFY(videoItem);

        QVideoFrame frame(QVideoFrameFormat(doc.video.size,
                                            QVideoFrameFormat::Format_BGRA8888));
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        for (int y = 0; y < frame.height(); ++y)
            std::memset(frame.bits(0) + y * frame.bytesPerLine(0), 0xff,
                        size_t(frame.width() * 4));
        frame.unmap();
        videoItem->videoSink()->setVideoFrame(frame);
        QCoreApplication::processEvents();

        tools->setTool(ToolType::Redact);
        tools->begin({32, 4});
        tools->finish({52, 24});

        videoItem->setVisible(false);
        QImage preview(doc.video.size, QImage::Format_ARGB32_Premultiplied);
        preview.fill(Qt::transparent);
        {
            QPainter painter(&preview);
            scene->render(&painter, QRectF(QPointF(), doc.video.size),
                          QRectF(QPointF(), doc.video.size));
        }
        videoItem->setVisible(true);
        QVERIFY(preview.pixelColor(14, 14).red() > 225);
        QVERIFY(preview.pixelColor(42, 14).red() > 225);

        const QImage overlay = window.exportComposite();
        QCOMPARE(overlay.pixelColor(14, 14).alpha(), 0);
        QCOMPARE(overlay.pixelColor(42, 14).alpha(), 0);

        auto *black = new RedactItem(RedactMode::Blacken, QImage(doc.video.size, QImage::Format_RGB32),
                                     QRectF(0, 30, 16, 16));
        scene->addItem(black);
        black->setFlag(QGraphicsItem::ItemIsSelectable);
        black->setSelected(true);
        const auto selected = scene->selectedItems();
        const int undoCount = window.findChild<QUndoStack *>()->count();
        QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        const QImage snapshot = QApplication::clipboard()->image();
        QCOMPARE(snapshot.size(), doc.video.size);
        QVERIFY(snapshot.pixelColor(14, 14).red() > 225); // Blur retained, not an empty overlay.
        QVERIFY(snapshot.pixelColor(8, 38).red() < 30);
        QCOMPARE(snapshot.pixelColor(60, 40), QColor(Qt::white));
        QCOMPARE(scene->selectedItems(), selected);
        QCOMPARE(window.findChild<QUndoStack *>()->count(), undoCount);
        QVERIFY(videoItem->isVisible());
    }
    void pausedVideoStaysVisibleBeforePlayback() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("still-with-audio.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "color=red:s=96x64:r=25:d=1", "-f", "lavfi", "-i",
            "color=blue:s=96x64:r=25:d=1", "-f", "lavfi", "-i",
            "anullsrc=r=48000:cl=stereo", "-filter_complex_threads", "1",
            "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0[v]",
            "-map", "[v]", "-map", "2:a", "-t", "2", "-c:v", "libx264",
            "-threads", "1", "-g", "25", "-pix_fmt", "yuv420p", "-c:a", "aac", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(96, 64), 2000, 25};
        EditorWindow window(doc, Config{}, {});
        window.show();
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        auto *player = window.findChild<QMediaPlayer *>();
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        auto *canvas = window.findChild<Canvas *>();
        QSignalSpy states(player, &QMediaPlayer::playbackStateChanged);
        const auto visibleColor = [&] {
            const QImage image = canvas->viewport()->grab().toImage();
            const QPoint point = canvas->mapFromScene(QPointF(48, 32)) * image.devicePixelRatio();
            return image.pixelColor(point);
        };
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTRY_VERIFY(visibleColor().red() > 200 && visibleColor().blue() < 40);
        QCOMPARE(player->position(), 0);
        for (const auto &change : states)
            QVERIFY(change.first().value<QMediaPlayer::PlaybackState>() != QMediaPlayer::PlayingState);

        // A cleared backend surface must not erase a paused, already decoded still.
        video->videoSink()->setVideoFrame(QVideoFrame());
        QVERIFY(visibleColor().red() > 200 && visibleColor().blue() < 40);
        QTest::keyClick(&window, Qt::Key_Space);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY(visibleColor().blue() > 200 && visibleColor().red() < 40);
        QTest::keyClick(&window, Qt::Key_Space);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PausedState);
        video->videoSink()->setVideoFrame(QVideoFrame());
        QVERIFY(visibleColor().blue() > 200 && visibleColor().red() < 40);
        window.copyVideoFrame();
        QVERIFY(QApplication::clipboard()->image().pixelColor(48, 32).blue() > 200);
        QCOMPARE(window.exportComposite().pixelColor(48, 32).alpha(), 0);
    }
    void shrunkVideoMatchesAnAreaFilteredFrame() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        // A one-pixel checkerboard turns into moiré unless the shrunk view is area-filtered.
        QTemporaryDir dir;
        const QString path = dir.filePath("checker.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "nullsrc=s=320x192:r=25:d=3,format=gray,geq=lum='if(mod(X+Y\\,2)\\,235\\,16)'",
            "-c:v", "libx264", "-qp", "0", "-threads", "1", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(320, 192), 3000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.resize(900, 640);
        window.show();
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        auto *player = window.findChild<QMediaPlayer *>();
        player->audioOutput()->setMuted(true);
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        auto *canvas = window.findChild<Canvas *>();
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        QTRY_COMPARE(video->childItems().size(), 1);
        QGraphicsItem *still = video->childItems().first();
        QTRY_VERIFY(still->isVisible());
        const auto difference = [&] {
            const QImage shown = canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
            const QRectF mapped = canvas->viewportTransform().mapRect(QRectF(0, 0, 320, 192));
            const QRect device(QPoint(qRound(mapped.left()), qRound(mapped.top())),
                               QPoint(qRound(mapped.right()) - 1, qRound(mapped.bottom()) - 1));
            const QImage reference = video->videoSink()->videoFrame().toImage()
                .scaled(device.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGB32);
            double sum = 0; int count = 0;
            for (int y = 2; y < device.height() - 2; ++y)
                for (int x = 2; x < device.width() - 2; ++x) {
                    const QRgb a = shown.pixel(device.topLeft() + QPoint(x, y)), b = reference.pixel(x, y);
                    sum += qAbs(qRed(a) - qRed(b)) + qAbs(qGreen(a) - qGreen(b)) + qAbs(qBlue(a) - qBlue(b));
                    count += 3;
                }
            return sum / qMax(1, count);
        };
        for (double zoom : {0.45, 0.72}) {
            canvas->restoreView(QTransform::fromScale(zoom, zoom), QPointF(160, 96), false);
            QVERIFY2(difference() < 2, qPrintable(QStringLiteral("paused %1: %2").arg(zoom).arg(difference())));
        }
        player->play();
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY(!still->isVisible());
        const double playing = difference();
        player->pause();
        QVERIFY2(playing < 2, qPrintable(QStringLiteral("playing: %1").arg(playing)));
        // Actual size stays an exact copy of the frame.
        canvas->resetZoom();
        QVERIFY2(difference() == 0, qPrintable(QString::number(difference())));
    }
    void startPastTheEndOpensTheRangeToTheClipsEnd() {
        // I behind the out point (or O before the in point) moves the other bound to the
        // clip's edge instead of squeezing the range to a single frame.
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = QStringLiteral("/tmp/eddy-io-past.mp4");
        doc.video = {QSize(64, 48), 10000, 25.0};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.show();
        auto *timeline = window.findChild<VideoTimeline *>();
        timeline->setPosition(2000); QTest::keyClick(&window, Qt::Key_I);
        timeline->setPosition(5000); QTest::keyClick(&window, Qt::Key_O);
        QCOMPARE(timeline->trimIn(), 2000); QCOMPARE(timeline->trimOut(), 5000);
        timeline->setPosition(8000); QTest::keyClick(&window, Qt::Key_I);
        QCOMPARE(timeline->trimIn(), 8000); QCOMPARE(timeline->trimOut(), 10000);
        timeline->setPosition(1000); QTest::keyClick(&window, Qt::Key_O);
        QCOMPARE(timeline->trimIn(), 0); QCOMPARE(timeline->trimOut(), 1000);
    }
    void aTrimThatKeepsNothingIsRefused() {
        // The second half is cut; a start inside it would leave nothing to play or export.
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = QStringLiteral("/tmp/eddy-no-kept-piece.mp4");
        doc.video = {QSize(64, 48), 10000, 25.0};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.show();
        StudioDocument studio = window.studioDocument();
        studio.fragments = {{0, 1.0, false}, {5000, 1.0, true}};
        window.setStudioDocument(studio);
        auto *field = window.findChild<QLineEdit *>(QStringLiteral("TrimInTime"));
        auto *timeline = window.findChild<VideoTimeline *>();
        field->setFocus();
        field->selectAll(); QTest::keyClicks(field, QStringLiteral("6.000")); QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(timeline->trimIn(), 0);
        QCOMPARE(field->text(), QStringLiteral("0:00.000"));
        QCOMPARE(window.findChild<Toast *>()->text(), QStringLiteral("Keep at least part of the clip"));
    }
    void changingTheRateWhilePausedLeavesThePlayheadAlone() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("rate.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=blue:s=96x64:r=25:d=3",
            "-c:v", "libx264", "-g", "10", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(96, 64), 3000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.show();
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        auto *player = window.findChild<QMediaPlayer *>();
        QTRY_VERIFY(player->isSeekable());
        StudioDocument studio = window.studioDocument();
        studio.fragments = {{0, 1.0, false}, {1000, 1.0, true}, {2000, 1.0, false}};
        window.setStudioDocument(studio);
        player->setPosition(1500);   // paused, inside the cut
        QTRY_VERIFY(qAbs(player->position() - 1500) <= 40);
        auto *speed = window.findChild<QToolButton *>(QStringLiteral("PlaybackSpeed"));
        for (auto *action : speed->menu()->actions())
            if (action->data().toDouble() == 2.0) action->trigger();
        QTest::qWait(300);
        QVERIFY2(qAbs(player->position() - 1500) <= 40, qPrintable(QString::number(player->position())));
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
    }
    void playAfterATrailingCutStartsOver() {
        // The last second is cut: playback ends at 2 s, and Play then starts from the top
        // instead of parking in the cut and stopping again at once.
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("tail.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=blue:s=96x64:r=25:d=3",
            "-c:v", "libx264", "-g", "10", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(96, 64), 3000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.show();
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        auto *player = window.findChild<QMediaPlayer *>();
        auto *play = window.findChild<QToolButton *>("PlaybackPlay");
        QTRY_VERIFY(player->isSeekable());
        StudioDocument studio = window.studioDocument();
        studio.fragments = {{0, 1.0, false}, {2000, 1.0, true}};
        window.setStudioDocument(studio);
        play->click();
        QTRY_COMPARE_WITH_TIMEOUT(player->playbackState(), QMediaPlayer::PlayingState, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(player->playbackState(), QMediaPlayer::PausedState, 6000);
        play->click();
        QTRY_VERIFY2_WITH_TIMEOUT(player->playbackState() == QMediaPlayer::PlayingState && player->position() < 1500,
            qPrintable(QStringLiteral("state %1 at %2 ms").arg(player->playbackState()).arg(player->position())), 4000);
        player->pause();
    }
    void realVideoScrubCopyLoopAndStop() {
        if (!have("ffmpeg")) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("seek.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "color=red:s=96x64:r=25:d=1.5", "-f", "lavfi", "-i",
            "color=blue:s=96x64:r=25:d=1.5", "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0",
            "-c:v", "libx264", "-g", "100", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(96, 64), 3040, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.show();
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        auto *player = window.findChild<QMediaPlayer *>();
        auto *timeline = window.findChild<VideoTimeline *>();
        auto *play = window.findChild<QToolButton *>("PlaybackPlay");
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        QTRY_VERIFY(player->isSeekable());
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        window.copyVideoFrame();
        QVERIFY(QApplication::clipboard()->image().pixelColor(48, 32).red() > 200);
        qint64 frameOrigin = video->videoSink()->videoFrame().startTime() < 0 ? -1
            : video->videoSink()->videoFrame().startTime() / 1000;
        connect(video->videoSink(), &QVideoSink::videoFrameChanged, &window, [&](const QVideoFrame &frame) {
            if (frameOrigin < 0 && frame.startTime() >= 0) frameOrigin = frame.startTime() / 1000;
        });
        play->click();
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        timeline->interactionStarted(false);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        timeline->seekRequested(2200);
        QApplication::clipboard()->setText("waiting");
        window.copyVideoFrame();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("waiting"));
        timeline->interactionFinished(false);
        QTRY_VERIFY2_WITH_TIMEOUT(!QApplication::clipboard()->image().isNull(), qPrintable(
            QStringLiteral("seek position=%1 frame=%2..%3 state=%4 toast=%5 origin=%6")
                .arg(player->position()).arg(video->videoSink()->videoFrame().startTime())
                .arg(video->videoSink()->videoFrame().endTime()).arg(player->playbackState())
                .arg(window.findChild<Toast *>()->text()).arg(frameOrigin)), 3000);
        const QColor copied = QApplication::clipboard()->image().pixelColor(48, 32);
        QVERIFY2(copied.blue() > 200 && copied.red() < 40, "Copied the stale red frame after a seek");
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        player->pause();

        // Two committed time edits define a short loop inside the first GOP.
        auto setTime = [&](const char *name, const char *value) {
            auto *field = window.findChild<QLineEdit *>(name);
            field->selectAll(); QTest::keyClicks(field, value); QTest::keyClick(field, Qt::Key_Return);
        };
        setTime("TrimInTime", "0.400");
        setTime("TrimOutTime", "0.960");
        QTRY_VERIFY(qAbs(video->videoSink()->videoFrame().startTime() / 1000 - frameOrigin - 400) <= 40);
        auto *loop = window.findChild<QToolButton *>("PlaybackLoop");
        loop->click();
        auto *speed = window.findChild<QToolButton *>("PlaybackSpeed");
        for (auto *action : speed->menu()->actions())
            if (action->data().toDouble() == 2.0) action->trigger();
        int wraps = 0;
        qint64 previous = 400 + frameOrigin;
        connect(video->videoSink(), &QVideoSink::videoFrameChanged, &window, [&](const QVideoFrame &frame) {
            const qint64 time = frame.startTime() / 1000;
            if (frame.isValid() && time >= 0) {
                if (time < previous - 200) ++wraps;
                previous = time;
            }
        });
        play->click();
        QTRY_VERIFY_WITH_TIMEOUT(wraps >= 2, 5000);
        QCOMPARE(player->playbackRate(), 2.0);
        loop->click();
        QTRY_COMPARE_WITH_TIMEOUT(player->playbackState(), QMediaPlayer::PausedState, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(video->videoSink()->videoFrame().startTime() / 1000
                                      - frameOrigin - 920) <= 1, 3000);
        QCOMPARE(window.findChild<QUndoStack *>()->count(), 2);

        window.findChild<QToolButton *>("TrimReset")->click();
        loop->click();
        wraps = 0;
        play->click();
        QTRY_VERIFY_WITH_TIMEOUT(wraps >= 1, 5000); // Backend end-of-media also loops.
        loop->click();
        QTRY_VERIFY_WITH_TIMEOUT(player->playbackState() != QMediaPlayer::PlayingState, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(video->videoSink()->videoFrame().startTime() / 1000 - frameOrigin >= 2960, 3000);
    }
    void videoFrameCopyKeepsRedactionSpotlightAndTextFocus() {
        MediaDocument doc; doc.kind = MediaKind::Video;
        doc.path = "/tmp/nonexistent-video-copy.mp4";
        doc.video = {QSize(160, 120), 1000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {}); window.show();
        QTRY_VERIFY(sceneHasVideoItem(window));
        auto *scene = window.findChild<QGraphicsScene *>();
        auto *player = window.findChild<QMediaPlayer *>();
        auto *video = qobject_cast<QGraphicsVideoItem *>(player->videoOutput());
        QVideoFrame frame(QVideoFrameFormat(doc.video.size, QVideoFrameFormat::Format_BGRA8888));
        QVERIFY(frame.map(QVideoFrame::WriteOnly));
        QImage pattern(frame.bits(0), frame.width(), frame.height(), frame.bytesPerLine(0), QImage::Format_ARGB32);
        for (int y = 0; y < pattern.height(); ++y)
            for (int x = 0; x < pattern.width(); ++x)
                pattern.setPixelColor(x, y, (x + y) % 2 ? Qt::white : Qt::black);
        const QImage source = pattern.copy();
        frame.unmap();
        video->videoSink()->setVideoFrame(frame);
        scene->addItem(new RedactItem(RedactMode::Blur, source, QRectF(10, 10, 40, 40)));
        auto *ocr = new RedactItem(RedactMode::OcrBlacken, source, QRectF(60, 10, 80, 40));
        ocr->setTextRects({QRectF(70, 20, 20, 20)}); scene->addItem(ocr);
        auto *spotlight = new SpotlightItem(QRectF(0, 0, 160, 60), doc.video.size);
        scene->addItem(spotlight);
        auto *tools = window.findChild<ToolController *>();
        tools->setTool(ToolType::Text);
        auto *text = static_cast<TextItem *>(tools->placeText(QPointF(10, 65)));
        text->setPlainText("Copy"); text->setFocus();
        const auto selection = scene->selectedItems();
        const int undo = window.findChild<QUndoStack *>()->count();
        QApplication::setActiveWindow(&window);
        QTest::keyClick(window.findChild<Canvas *>(), Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        const auto copied = QApplication::clipboard()->image();
        const int blurred = copied.pixelColor(30, 30).red();
        QVERIFY(blurred > 70 && blurred < 190);
        QVERIFY(copied.pixelColor(75, 25).red() < 30);
        QVERIFY(copied.pixelColor(150, 91).red() < source.pixelColor(150, 91).red());
        QCOMPARE(scene->focusItem(), text);
        QCOMPARE(scene->selectedItems(), selection);
        QVERIFY(text->textInteractionFlags().testFlag(Qt::TextEditable));
        QCOMPARE(window.findChild<QUndoStack *>()->count(), undo);
    }
    void narrowVideoKeepsControlsInsideWindow_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("dark") << true;
        QTest::newRow("light") << false;
    }
    void narrowVideoKeepsControlsInsideWindow() {
        QFETCH(bool, dark);
        QVERIFY2(!QFontDatabase::families().isEmpty(),
                 "Offscreen layout tests need system fonts; set QT_QPA_FONTDIR");
        const QString oldStyle = qApp->styleSheet();
        const auto restoreStyle = qScopeGuard([&] { qApp->setStyleSheet(oldStyle); });
        qApp->setStyleSheet(theme::styleSheet(dark));
        MediaDocument doc; doc.kind = MediaKind::Video;
        doc.path = "/tmp/nonexistent-video-layout.mp4";
        doc.video = {QSize(640, 360), 36000000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.resize(520, 620); window.show();
        QCoreApplication::processEvents();
        QCOMPARE(window.width(), 520);
        for (const auto *name : {"PlaybackPlay", "PlaybackLoop", "PlaybackSpeed", "PlaybackMute",
                                 "TrimSetIn", "TrimInTime", "TrimSetOut", "TrimOutTime",
                                 "TrimDurationCaption", "TrimDuration", "TrimReset"}) {
            const auto *control = window.findChild<QWidget *>(name);
            QVERIFY(control && control->isVisible());
            QVERIFY2(window.rect().contains(QRect(control->mapTo(&window, QPoint()), control->size())), name);
        }
        QVERIFY(!window.findChild<QSlider *>("PlaybackVolume")->isVisible());
        for (const auto *name : {"TrimInTime", "TrimOutTime"}) {
            auto *field = window.findChild<QLineEdit *>(name);
            field->setCursorPosition(0);
            const qreal start = field->inputMethodQuery(Qt::ImCursorRectangle).toRectF().x();
            field->setCursorPosition(field->text().size());
            const qreal end = field->inputMethodQuery(Qt::ImCursorRectangle).toRectF().x();
            // A field that scrolls horizontally hides leading digits before
            // the first trim. All digits must fit using the styled font.
            QVERIFY2(end - start >= field->fontMetrics().horizontalAdvance(field->text()) - 1, name);
        }
        window.resize(1200, 680);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QSlider *>("PlaybackVolume")->isVisible(), 1000);
    }
    void videoMenusOpenFromKeyboardWithoutRunningPrimaryAction() {
        MediaDocument doc; doc.kind = MediaKind::Video;
        doc.path = "/tmp/nonexistent-video-menu.mp4";
        doc.video = {QSize(640, 360), 10000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.resize(520, 620); window.show();
        QCoreApplication::processEvents();
        for (const auto *name : {"PlaybackMute", "Copy", "PlaybackSpeed"}) {
            auto *button = window.findChild<QToolButton *>(name);
            QVERIFY(button && button->menu());
            auto *menu = button->menu();
            QSignalSpy opened(menu, &QMenu::aboutToShow);
            QSignalSpy clicked(button, &QToolButton::clicked);
            connect(menu, &QMenu::aboutToShow, menu, [menu] {
                QTimer::singleShot(0, menu, &QMenu::close);
            });
            QTest::keyClick(button, Qt::Key_Down, Qt::AltModifier);
            QCOMPARE(opened.count(), 1);
            QCOMPARE(clicked.count(), 0);
        }
    }
    void missingVideoFrameDoesNotReplaceClipboard() {
        MediaDocument doc; doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-video-frame-test.mp4");
        doc.video = {QSize(64, 48), 1000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        QApplication::clipboard()->setText("keep me");
        window.copyVideoFrame();
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep me"));
        QCOMPARE(window.findChild<Toast *>()->text(), QStringLiteral("Frame unavailable"));
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
        auto *inTime = w.findChild<QLineEdit *>(QStringLiteral("TrimInTime"));
        auto *outTime = w.findChild<QLineEdit *>(QStringLiteral("TrimOutTime"));
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
    void loopAndSpeedArePreviewOnlyAndFrameStepsDoNotDrift() {
        MediaDocument doc; doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video = {QSize(64, 48), 60000, 30000.0 / 1001};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        auto *loop = window.findChild<QToolButton *>("PlaybackLoop");
        auto *rate = window.findChild<QToolButton *>("PlaybackSpeed");
        auto *timeline = window.findChild<VideoTimeline *>();
        auto *undo = window.findChild<QUndoStack *>();
        QVERIFY(loop && rate && timeline && undo);
        loop->click();
        QVERIFY(loop->isChecked());
        for (auto *action : rate->menu()->actions())
            if (action->data().toDouble() == 1.5) action->trigger();
        QCOMPARE(window.findChild<QMediaPlayer *>()->playbackRate(), 1.5);
        for (int i = 0; i < 300; ++i) QTest::keyClick(&window, Qt::Key_L);
        QCOMPARE(timeline->position(), 10010);
        QTest::keyClick(&window, Qt::Key_J);
        QCOMPARE(timeline->position(), 9977);
        QCOMPARE(undo->count(), 0);
        QVERIFY(!window.findChild<QTimer *>("VideoExportTimer")->isActive());
    }
    void trimTimeEditingCommitsOnceAndEscapeDoesNotClose() {
        MediaDocument doc; doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-editorwindow-test.mp4");
        doc.video = {QSize(64, 48), 10000, 25};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        auto *field = window.findChild<QLineEdit *>("TrimInTime");
        auto *timeline = window.findChild<VideoTimeline *>();
        auto *undo = window.findChild<QUndoStack *>();
        QVERIFY(field && timeline && undo);
        field->selectAll(); QTest::keyClicks(field, "1.250");
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(timeline->trimIn(), 1250);
        QCOMPARE(undo->count(), 1);
        QCOMPARE(field->text(), QStringLiteral("0:01.250"));
        field->selectAll(); QTest::keyClicks(field, "9:99");
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(timeline->trimIn(), 1250);
        QCOMPARE(undo->count(), 1);
        field->selectAll(); QTest::keyClicks(field, "2");
        QTest::keyClick(field, Qt::Key_Escape);
        QCOMPARE(field->text(), QStringLiteral("0:01.250"));
        QCOMPARE(undo->count(), 1);
        QCOMPARE(window.findChild<QLabel *>("TrimDuration")->text(), QStringLiteral("0:08.750"));
        undo->undo();
        QCOMPARE(timeline->trimIn(), 0);
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
        QTRY_VERIFY_WITH_TIMEOUT(toast->text().startsWith(QStringLiteral("Video export failed: ")), 5000);
        QVERIFY2(toast->text().contains(QStringLiteral("No such file")), qPrintable(toast->text()));
        const auto *status = w.findChild<QLabel *>(QStringLiteral("VideoExportStatus"));
        QVERIFY(status);
        QCOMPARE(status->text(), QStringLiteral("Export failed"));
        QVERIFY(status->toolTip().contains(QStringLiteral("eddy-missing-video-export-test")));
        const auto *exportTimer = w.findChild<QTimer *>(QStringLiteral("VideoExportTimer"));
        QVERIFY(exportTimer);
        QVERIFY(!exportTimer->isActive());
    }
    void anEditStopsTheStaleExportAndStartsAFreshOne() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake ffmpeg helper is not available on Windows");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString calls = dir.filePath(QStringLiteral("calls"));
        QFile script(dir.filePath(QStringLiteral("ffmpeg")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        // Fail the hardware probes quickly, then note the encoder's pid and hang.
        script.write(QStringLiteral("#!/bin/sh\ncase \"$*\" in *lavfi*) exit 1;; esac\n"
                                    "echo $$ >> '%1'\nwhile :; do sleep 0.05; done\n").arg(calls).toUtf8());
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);
        const auto encoders = [&] {
            QFile file(calls);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts) : QStringList();
        };

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/eddy-stale-video-export-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        doc.video.fps = 25.0;
        Config cfg; cfg.animations = false;
        EditorWindow w(doc, cfg, CliOptions());
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *cancel = w.findChild<QToolButton *>(QStringLiteral("VideoExportCancel"));
        auto *status = w.findChild<QLabel *>(QStringLiteral("VideoExportStatus"));
        QVERIFY(timeline && cancel && status);
        timeline->setPosition(200);
        QTest::keyClick(&w, Qt::Key_I);
        w.sendToShelf();
        QTRY_COMPARE_WITH_TIMEOUT(encoders().size(), 1, 5000);
        const QString stale = encoders().first();

        timeline->setPosition(800);
        QTest::keyClick(&w, Qt::Key_O);
        QTRY_COMPARE_WITH_TIMEOUT(encoders().size(), 2, 5000);
        QVERIFY2(QProcess::execute(QStringLiteral("kill"), {QStringLiteral("-0"), stale}) != 0,
                 "the stale encoder still runs");

        QTRY_VERIFY_WITH_TIMEOUT(!cancel->isHidden(), 5000);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("Edited"), 2000);
        qputenv("PATH", oldPath);
        QVERIFY2(w.close(), "window stayed blocked after cancelling the export");
#endif
    }
    void runningVideoExportCanBeCancelled() {
#ifdef Q_OS_WIN
        QSKIP("POSIX fake ffmpeg helper is not available on Windows");
#else
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile script(dir.filePath(QStringLiteral("ffmpeg")));
        QVERIFY(script.open(QIODevice::WriteOnly));
        // Fail the hardware probes quickly, then hang like a stuck encoder.
        script.write("#!/bin/sh\ncase \"$*\" in *lavfi*) exit 1;; esac\nwhile :; do sleep 0.05; done\n");
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(dir.path()) + ':' + oldPath);

        MediaDocument doc;
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/eddy-cancel-video-export-test.mp4");
        doc.video.size = QSize(64, 48);
        doc.video.durationMs = 1000;
        doc.video.fps = 25.0;
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(doc, cfg, cli);
        auto *timeline = w.findChild<VideoTimeline *>();
        auto *toast = w.findChild<Toast *>();
        auto *cancel = w.findChild<QToolButton *>(QStringLiteral("VideoExportCancel"));
        auto *status = w.findChild<QLabel *>(QStringLiteral("VideoExportStatus"));
        QVERIFY(timeline && toast && cancel && status);
        QVERIFY(cancel->isHidden());
        timeline->setPosition(200);
        QTest::keyClick(&w, Qt::Key_I);

        w.sendToShelf();
        QTRY_VERIFY_WITH_TIMEOUT(!cancel->isHidden(), 5000);
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(toast->text(), QStringLiteral("Video export cancelled"));
        QVERIFY(cancel->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("Edited"), 2000);
        qputenv("PATH", oldPath);
        QCOMPARE(toast->text(), QStringLiteral("Video export cancelled"));
        QVERIFY2(w.close(), "window stayed blocked after cancelling the export");
#endif
    }
};
QTEST_MAIN(TestEditorVideo)
#include "test_editorvideo.moc"

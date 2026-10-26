#include "videotest.h"

class TestVideoRender : public QObject {
    Q_OBJECT
private slots:
    void renderedExportMatchesTheFilterGraph() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        // Red left half, blue right half: the blur below straddles the edge.
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=red:s=160x240:d=1:r=10", "-f", "lavfi", "-i", "color=c=blue:s=160x240:d=1:r=10",
            "-filter_complex", "[0:v][1:v]hstack", "-pix_fmt", "yuv420p", input}));
        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter(&overlay).fillRect(QRect(40, 40, 40, 40), Qt::green);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("graph.mp4")), overlay};
        request.cropRect = QRect(20, 20, 280, 200);
        request.blurRects = {QRect(140, 100, 40, 40)};
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = QColor(255, 255, 0);
        request.studio.padding = 10;
        request.studio.radius = 10;
        request.studio.shadow = 0;
        const DeliverResult graph = writeVideoWithOverlay(request);
        QVERIFY2(graph.ok, qPrintable(graph.error));
        const QString graphPath = request.outputPath;
        request.outputPath = dir.filePath(QStringLiteral("rendered.mp4"));
        const DeliverResult rendered = writeVideoRendered(request);
        QVERIFY2(rendered.ok, qPrintable(rendered.error));

        const auto a = probeVideoFile(graphPath), b = probeVideoFile(request.outputPath);
        QVERIFY(a.ok && b.ok);
        QCOMPARE(b.info.size, a.info.size);
        QVERIFY2(qAbs(a.info.durationMs - b.info.durationMs) <= 100,
                 qPrintable(QStringLiteral("%1 vs %2 ms").arg(a.info.durationMs).arg(b.info.durationMs)));
        QCOMPARE(qRound(b.info.fps), 60);
        auto frameAt = [&](const QString &video, const QString &name) {
            const QString png = dir.filePath(name);
            return runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", video,
                                                         "-frames:v", "1", png}) ? QImage(png) : QImage();
        };
        const QImage fromGraph = frameAt(graphPath, QStringLiteral("graph.png"));
        const QImage fromRender = frameAt(request.outputPath, QStringLiteral("rendered.png"));
        QVERIFY(!fromGraph.isNull() && fromGraph.size() == fromRender.size());
        const QPoint origin = studioLayout(QSize(280, 200), request.studio).content.topLeft();
        // Background, red, blue, the annotation and the middle of the blur.
        for (const QPoint &p : {QPoint(4, 4), origin + QPoint(60, 150), origin + QPoint(220, 150),
                                origin + QPoint(40, 40), origin + QPoint(140, 100)}) {
            const QColor g = fromGraph.pixelColor(p), r = fromRender.pixelColor(p);
            QVERIFY2(qAbs(g.red() - r.red()) <= 10 && qAbs(g.green() - r.green()) <= 10
                         && qAbs(g.blue() - r.blue()) <= 10,
                     qPrintable(QStringLiteral("%1,%2: %3 vs %4").arg(p.x()).arg(p.y())
                                    .arg(g.name(), r.name())));
        }
        qint64 difference = 0;
        for (int y = 0; y < fromGraph.height(); ++y)
            for (int x = 0; x < fromGraph.width(); ++x) {
                const QRgb g = fromGraph.pixel(x, y), r = fromRender.pixel(x, y);
                difference += qAbs(qRed(g) - qRed(r)) + qAbs(qGreen(g) - qGreen(r)) + qAbs(qBlue(g) - qBlue(r));
            }
        const double mean = double(difference) / (3.0 * fromGraph.width() * fromGraph.height());
        QVERIFY2(mean < 3.0, qPrintable(QStringLiteral("mean difference %1").arg(mean)));
    }

    void zoomSegmentFillsTheOutputWithItsTarget() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage quadrants(320, 240, QImage::Format_RGB32);
        {
            QPainter p(&quadrants);
            p.fillRect(0, 0, 160, 120, Qt::red);
            p.fillRect(160, 0, 160, 120, Qt::green);
            p.fillRect(0, 120, 160, 120, Qt::blue);
            p.fillRect(160, 120, 160, 120, Qt::white);
        }
        const QString still = dir.filePath(QStringLiteral("quadrants.png"));
        QVERIFY(quadrants.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
        QImage overlay(320, 240, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(240, 60),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY(probe.ok);
        QCOMPARE(probe.info.size, QSize(320, 240));
        QCOMPARE(qRound(probe.info.fps), 60);   // zooms went through the frame renderer
        // Qt draws RGB; the output says which matrix turned it into YUV, so players do not guess.
        QCOMPARE(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "v:0",
                 "-show_entries", "stream=color_space", "-of", "csv=p=0", request.outputPath}),
                 QByteArray("bt709"));
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QImage decoded(frame);
        for (const QPoint &p : {QPoint(20, 20), QPoint(300, 20), QPoint(20, 220), QPoint(300, 220), QPoint(160, 120)}) {
            const QColor c = decoded.pixelColor(p);
            QVERIFY2(c.green() > 200 && c.red() < 60 && c.blue() < 60,
                     qPrintable(QStringLiteral("%1,%2 is %3").arg(p.x()).arg(p.y()).arg(c.name())));
        }
    }

    void motionBlurSmearsOnlyAMovingCamera() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "2",
            "-r", "30", "-vf", "setsar=1,format=yuv444p", input}));
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        // Zooming in on a point right of the edge slides the edge left while the camera moves.
        VideoExportRequest request{input, QString(), overlay};
        request.zooms = {{1, 400, 2000, 2.0, ZoomSegment::Target::Point, QPointF(200, 90),
                          ZoomSegment::Motion::Focused}};
        request.maxFps = 10;   // a long frame, so half of it is a clear smear
        // The widest mixed red and blue run across the middle row, per frame.
        auto edges = [&](int blur) {
            request.motionBlur = blur;
            request.outputPath = dir.filePath(QStringLiteral("out%1.mp4").arg(blur));
            const DeliverResult result = writeVideoRendered(request);
            if (!result.ok) return QVector<int>();
            const QString pattern = dir.filePath(QStringLiteral("b%1-%03d.png").arg(blur));
            if (!runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-i", request.outputPath, pattern}))
                return QVector<int>();
            QVector<int> widths;
            for (int i = 1;; ++i) {
                const QImage frame(dir.filePath(QStringLiteral("b%1-%2.png").arg(blur).arg(i, 3, 10, QLatin1Char('0'))));
                if (frame.isNull()) break;
                int mixed = 0;
                for (int x = 0; x < frame.width(); ++x) {
                    const QColor c = frame.pixelColor(x, 90);
                    mixed += c.red() > 50 && c.blue() > 50;
                }
                widths.append(mixed);
            }
            return widths;
        };
        const QVector<int> sharp = edges(0), smeared = edges(100);
        QVERIFY(!sharp.isEmpty());
        QCOMPARE(smeared.size(), sharp.size());
        // Before the zoom the camera stands still: no smear there.
        QVERIFY2(qAbs(smeared.first() - sharp.first()) <= 1,
                 qPrintable(QStringLiteral("%1 vs %2").arg(smeared.first()).arg(sharp.first())));
        const int widest = *std::max_element(smeared.cbegin(), smeared.cend());
        const int widestSharp = *std::max_element(sharp.cbegin(), sharp.cend());
        QVERIFY2(widest >= widestSharp + 6, qPrintable(QStringLiteral("%1 vs %2").arg(widest).arg(widestSharp)));
    }

    void keepZoomedInCropsToTheFramesRatio() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay};
        request.studio.background = StudioStyle::Background::Color;
        request.studio.color = Qt::green;
        request.studio.padding = 0;
        request.studio.radius = 0;
        request.studio.shadow = 0;
        request.studio.aspect = QSize(9, 16);
        request.baseView = keepZoomedInRect(QRect(0, 0, 320, 180), request.studio, QPointF(240, 90));
        QCOMPARE(request.baseView, QRect(190, 0, 100, 180));
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY(probe.ok);
        QCOMPARE(probe.info.size, QSize(102, 180));   // 9:16 with a 2 px background grow
        QCOMPARE(qRound(probe.info.fps), 30);         // a static crop stays on the filter graph
        const QString frame = dir.filePath(QStringLiteral("frame.png"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        const QColor c = QImage(frame).pixelColor(51, 90);
        QVERIFY2(c.blue() > 200 && c.red() < 60, qPrintable(c.name()));

        // With a zoom the renderer takes over and zooms inside the same window.
        request.outputPath = dir.filePath(QStringLiteral("zoom.mp4"));
        request.baseView = QRect(110, 0, 100, 180);    // straddles both halves
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(135, 90),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult zoomed = writeVideoWithOverlay(request);
        QVERIFY2(zoomed.ok, qPrintable(zoomed.error));
        const auto zoomProbe = probeVideoFile(request.outputPath);
        QCOMPARE(zoomProbe.info.size, QSize(102, 180));
        QCOMPARE(qRound(zoomProbe.info.fps), 60);
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", "0.5", "-i", request.outputPath,
                                                     "-frames:v", "1", frame}));
        // 2x around x = 135 shows 110..160 (red) across the whole width.
        const QImage decoded(frame);
        for (int x : {8, 51, 94}) {
            const QColor p = decoded.pixelColor(x, 90);
            QVERIFY2(p.red() > 200 && p.blue() < 60, qPrintable(QStringLiteral("%1: %2").arg(x).arg(p.name())));
        }
    }
    void theCameraFollowsThePointerTrack() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still, "-t", "1",
            "-r", "30", "-vf", "setsar=1,format=yuv420p", input}));
        auto track = std::make_shared<CursorTrack>();
        track->videoSize = QSize(320, 180);
        track->samples = {{0, QPointF(300, 90), true}};   // the pointer rests on the blue side
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        auto centre = [&](const QString &file) {
            const QString frame = dir.filePath(QStringLiteral("frame.png"));
            runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", "0.5", "-i", file, "-frames:v", "1", frame});
            const QImage image(frame);
            return image.pixelColor(image.width() / 2, image.height() / 2);
        };
        // A Cursor zoom looks where the pointer is, whatever its point says.
        VideoExportRequest zoomed{input, dir.filePath(QStringLiteral("zoom.mp4")), overlay};
        zoomed.cursorTrack = track;
        zoomed.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Cursor, QPointF(20, 20), ZoomSegment::Motion::Instant}};
        QVERIFY2(writeVideoWithOverlay(zoomed).ok, "cursor zoom export failed");
        QVERIFY2(centre(zoomed.outputPath).blue() > 200, qPrintable(centre(zoomed.outputPath).name()));
        // A narrowed base view follows it too, without any zoom.
        VideoExportRequest follows{input, dir.filePath(QStringLiteral("follow.mp4")), overlay};
        follows.cursorTrack = track;
        follows.baseView = QRect(110, 0, 100, 180);
        follows.baseFollowsCursor = true;
        QVERIFY(writeVideoWithOverlay(follows).ok);
        QCOMPARE(qRound(probeVideoFile(follows.outputPath).info.fps), 60);   // it moves, so it renders
        QVERIFY2(centre(follows.outputPath).blue() > 200, qPrintable(centre(follows.outputPath).name()));
    }
    void timeWindowsLimitBlurAndOverlays() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        // Sharp one-pixel stripes, so a blur shows as lost contrast.
        const QString input = dir.filePath(QStringLiteral("stripes.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "nullsrc=s=64x48:r=25:d=3,format=gray,geq=lum='if(mod(X\\,2)\\,235\\,16)'",
            "-c:v", "libx264", "-qp", "0", "-pix_fmt", "yuv444p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QImage red(64, 48, QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::transparent);
        QPainter(&red).fillRect(40, 4, 20, 16, Qt::red);
        auto frameAt = [&](const QString &file, double seconds) {
            const QString frame = dir.filePath(QStringLiteral("f.png"));
            runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", QString::number(seconds), "-i", file,
                                                  "-frames:v", "1", frame});
            return QImage(frame).convertToFormat(QImage::Format_RGB32);
        };
        for (bool render : {false, true}) {
            // Trimmed from 0.5 s: windows stay in source time, 1 to 2 s.
            VideoExportRequest request{input, dir.filePath(render ? QStringLiteral("r.mp4") : QStringLiteral("g.mp4")),
                                       overlay, 500, 3000};
            request.timedBlurs = {{QRect(0, 24, 32, 24), 1000, 2000}};
            request.timedOverlays = {{red, 1000, 2000}};
            if (render) request.zooms = {{1, 0, 100, 1.1, ZoomSegment::Target::Point, QPointF(32, 24),
                                          ZoomSegment::Motion::Instant}};
            const DeliverResult result = writeVideoWithOverlay(request);
            QVERIFY2(result.ok, qPrintable(result.error));
            const QRect blurred(4, 28, 24, 16);
            const QImage before = frameAt(request.outputPath, 0.2), inside = frameAt(request.outputPath, 1.0),
                         after = frameAt(request.outputPath, 2.0);   // output time: source 0.7, 1.5, 2.5 s
            QVERIFY2(horizontalContrast(before, blurred) > 3 * horizontalContrast(inside, blurred),
                     qPrintable(QStringLiteral("render %1: %2 vs %3").arg(render).arg(horizontalContrast(before, blurred))
                                    .arg(horizontalContrast(inside, blurred))));
            QVERIFY(horizontalContrast(after, blurred) > 3 * horizontalContrast(inside, blurred));
            auto isRed = [](const QImage &image) {
                const QColor c = image.pixelColor(50, 12);
                return c.red() > 180 && c.green() < 90;
            };
            QVERIFY2(!isRed(before) && isRed(inside) && !isRed(after), qPrintable(QStringLiteral("render %1").arg(render)));
        }
    }
    void rejectsABaseViewOutsideTheCrop() {
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        VideoExportRequest request{QStringLiteral("in.mp4"), QStringLiteral("out.mp4"), overlay};
        request.cropRect = QRect(0, 0, 160, 180);
        request.baseView = QRect(100, 0, 100, 180);
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("view")), qPrintable(result.error));
    }
    void shrinksAndCapsFramesForSmallerExports() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "testsrc2=s=640x360:r=60:d=1", "-f", "lavfi", "-i", "sine=frequency=440:duration=1",
            "-shortest", "-pix_fmt", "yuv420p", input}));
        QImage overlay(640, 360, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("small.mp4")), overlay};
        request.maxShortSide = 180;
        request.maxFps = 30;
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QCOMPARE(probe.info.size, QSize(320, 180));
        QCOMPARE(qRound(probe.info.fps), 30);
        // The same through the frame renderer.
        request.outputPath = dir.filePath(QStringLiteral("small-zoom.mp4"));
        request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(320, 180),
                          ZoomSegment::Motion::Instant}};
        const DeliverResult rendered = writeVideoWithOverlay(request);
        QVERIFY2(rendered.ok, qPrintable(rendered.error));
        const auto renderedProbe = probeVideoFile(request.outputPath);
        QCOMPARE(renderedProbe.info.size, QSize(320, 180));
        QCOMPARE(qRound(renderedProbe.info.fps), 30);
    }
    void exportsAPalettedGifWithoutSound() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QImage halves(320, 180, QImage::Format_RGB32);
        {
            QPainter p(&halves);
            p.fillRect(0, 0, 160, 180, Qt::red);
            p.fillRect(160, 0, 160, 180, Qt::blue);
        }
        const QString still = dir.filePath(QStringLiteral("halves.png"));
        QVERIFY(halves.save(still));
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-loop", "1", "-i", still,
            "-f", "lavfi", "-i", "sine=frequency=440:duration=1", "-t", "1", "-r", "30",
            "-vf", "setsar=1,format=yuv420p", "-shortest", input}));
        QImage overlay(320, 180, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        for (bool zoom : {false, true}) {
            VideoExportRequest request{input, dir.filePath(zoom ? QStringLiteral("z.gif") : QStringLiteral("a.gif")), overlay};
            request.maxShortSide = 90;
            request.maxFps = 15;
            if (zoom)
                request.zooms = {{1, 0, 1000, 2.0, ZoomSegment::Target::Point, QPointF(80, 90),
                                  ZoomSegment::Motion::Instant}};
            const DeliverResult result = writeVideoWithOverlay(request);
            QVERIFY2(result.ok, qPrintable(result.error));
            QCOMPARE(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "v:0",
                     "-show_entries", "stream=codec_name,width,height,r_frame_rate", "-of", "csv=p=0",
                     request.outputPath}), QByteArray("gif,160,90,15/1"));
            QVERIFY(processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams", "a",
                    "-show_entries", "stream=index", "-of", "csv=p=0", request.outputPath}).isEmpty());
            const QString frame = dir.filePath(QStringLiteral("frame.png"));
            QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", "0.5", "-i",
                                                         request.outputPath, "-frames:v", "1", frame}));
            // Zoomed 2x onto the red half, every sample is red; otherwise the halves stay apart.
            const QImage decoded(frame);
            const QColor left = decoded.pixelColor(20, 45), right = decoded.pixelColor(140, 45);
            QVERIFY2(left.red() > 200 && left.blue() < 60, qPrintable(left.name()));
            if (zoom) QVERIFY2(right.red() > 200 && right.blue() < 60, qPrintable(right.name()));
            else QVERIFY2(right.blue() > 200 && right.red() < 60, qPrintable(right.name()));
        }
    }
    void fragmentsCutAndSpeedUpPictureAndSound() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        // Six seconds, one colour per second, a short beep at the start of every second.
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error",
            "-f", "lavfi", "-i", "color=red:s=64x48:r=30:d=1", "-f", "lavfi", "-i", "color=0x00ff00:s=64x48:r=30:d=1",
            "-f", "lavfi", "-i", "color=blue:s=64x48:r=30:d=1", "-f", "lavfi", "-i", "color=yellow:s=64x48:r=30:d=1",
            "-f", "lavfi", "-i", "color=white:s=64x48:r=30:d=1", "-f", "lavfi", "-i", "color=black:s=64x48:r=30:d=1",
            "-f", "lavfi", "-i", "sine=frequency=1000:duration=6,volume='if(lt(mod(t,1),0.05),1,0)':eval=frame",
            "-filter_complex", "[0:v][1:v][2:v][3:v][4:v][5:v]concat=n=6:v=1:a=0[v]",
            "-map", "[v]", "-map", "6:a", "-pix_fmt", "yuv420p", "-c:a", "aac", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        // Keep 0-2 s, cut 2-3 s (blue), 3-5 s at 2x (yellow, white), keep 5-6 s.
        const QVector<Fragment> fragments{{0, 1.0, false}, {2000, 1.0, true}, {3000, 2.0, false}, {5000, 1.0, false}};
        auto colourAt = [&](const QString &file, double seconds) {
            const QString frame = dir.filePath(QStringLiteral("f.png"));
            runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-y", "-ss", QString::number(seconds), "-i", file,
                                                  "-frames:v", "1", frame});
            return QImage(frame).pixelColor(32, 24);
        };
        auto duration = [&](const QString &file) {
            return processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-show_entries", "format=duration",
                                                             "-of", "csv=p=0", file}).toDouble();
        };
        for (bool render : {false, true}) {
            VideoExportRequest request{input, dir.filePath(render ? QStringLiteral("r.mp4") : QStringLiteral("g.mp4")), overlay};
            request.fragments = fragments;
            if (render) request.zooms = {{1, 0, 100, 1.1, ZoomSegment::Target::Point, QPointF(32, 24),
                                          ZoomSegment::Motion::Instant}};
            const DeliverResult result = writeVideoWithOverlay(request);
            QVERIFY2(result.ok, qPrintable(result.error));
            // 2 s + 1 s (2 s at 2x) + 1 s = 4 s.
            QVERIFY2(qAbs(duration(request.outputPath) - 4.0) < 0.1,
                     qPrintable(QStringLiteral("render %1: %2 s").arg(render).arg(duration(request.outputPath))));
            const struct { double at; QColor want; } probes[] = {
                {0.5, Qt::red}, {1.5, Qt::green}, {2.25, Qt::yellow}, {2.75, Qt::white}, {3.5, Qt::black}};
            for (const auto &probe : probes) {
                const QColor c = colourAt(request.outputPath, probe.at);
                QVERIFY2(qAbs(c.red() - probe.want.red()) < 60 && qAbs(c.green() - probe.want.green()) < 60
                             && qAbs(c.blue() - probe.want.blue()) < 60,
                         qPrintable(QStringLiteral("render %1 at %2: %3").arg(render).arg(probe.at).arg(c.name())));
            }
            // Beeps start at 0 and 1 s (kept), 2 s (source 3 s) and 2.5 s (source 4 s, at 2x), 3 s (source 5 s).
            const QByteArray pcm = rawOutput(QStringLiteral("ffmpeg"), {"-v", "error", "-i", request.outputPath,
                "-ac", "1", "-ar", "8000", "-f", "s16le", "-"});
            const auto *samples = reinterpret_cast<const qint16 *>(pcm.constData());
            QVector<double> onsets;
            int quiet = 400;
            for (qsizetype i = 0; i < pcm.size() / 2; ++i) {
                if (qAbs(samples[i]) > 3000) {
                    if (quiet >= 400) onsets.append(i / 8000.0);
                    quiet = 0;
                } else {
                    ++quiet;
                }
            }
            const QVector<double> want{0, 1, 2, 2.5, 3};
            QVERIFY2(onsets.size() == want.size(), qPrintable(QStringLiteral("render %1: %2 beeps").arg(render).arg(onsets.size())));
            for (int i = 0; i < want.size(); ++i)
                QVERIFY2(qAbs(onsets[i] - want[i]) < 0.06,
                         qPrintable(QStringLiteral("render %1 beep %2 at %3 s").arg(render).arg(i).arg(onsets[i])));
        }
    }
    void refusesARangeThatIsAllCut() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=6:r=25", "-pix_fmt", "yuv420p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay, 2000, 4000};
        request.fragments = {{0, 1.0, false}, {1000, 1.0, true}, {5000, 1.0, false}};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY(!result.ok);
        QVERIFY2(result.error.contains(QStringLiteral("cut")), qPrintable(result.error));
        QVERIFY(!QFileInfo::exists(request.outputPath));
    }
    void renderedExportKeepsTrimAndAudio() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=2:r=25", "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
            "-shortest", "-pix_fmt", "yuv420p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        VideoExportRequest request{input, dir.filePath(QStringLiteral("out.mp4")), overlay, 500, 1500};
        request.zooms = {{1, 0, 2000, 1.5, ZoomSegment::Target::Point, QPointF(32, 24),
                          ZoomSegment::Motion::Focused}};
        const DeliverResult result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto probe = probeVideoFile(request.outputPath);
        QVERIFY2(probe.ok, qPrintable(probe.error));
        QVERIFY2(qAbs(probe.info.durationMs - 1000) <= 80,
                 qPrintable(QStringLiteral("duration was %1 ms").arg(probe.info.durationMs)));
        const QByteArray audio = processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-select_streams",
            "a:0", "-show_entries", "stream=index", "-of", "csv=p=0", request.outputPath});
        QVERIFY2(!audio.isEmpty(), "the rendered export lost its audio stream");
    }
    void withoutAudioNoRouteKeepsAnAudioStream() {
        if (!have(QStringLiteral("ffmpeg")) || !have(QStringLiteral("ffprobe")))
            QSKIP("ffmpeg/ffprobe not available");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString input = dir.filePath(QStringLiteral("input.mp4"));
        QVERIFY(runProcess(QStringLiteral("ffmpeg"), {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=2:r=25", "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
            "-shortest", "-pix_fmt", "yuv420p", input}));
        QImage overlay(64, 48, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        auto streams = [&](const QString &path) {
            return processOutput(QStringLiteral("ffprobe"), {"-v", "error", "-show_entries", "stream=codec_type",
                                                             "-of", "csv=p=0", path}).trimmed();
        };
        const QVector<Fragment> cut = {{0, 1.0, false}, {800, 1.0, true}, {1200, 1.0, false}};
        const ZoomSegment zoom{1, 0, 2000, 1.5, ZoomSegment::Target::Point, QPointF(32, 24),
                               ZoomSegment::Motion::Focused};
        // The plain graph, the graph with cut sound, the frame renderer.
        for (int route = 0; route < 3; ++route) {
            VideoExportRequest request{input, dir.filePath(QStringLiteral("out%1.mp4").arg(route)), overlay};
            request.includeAudio = false;
            if (route == 1) request.fragments = cut;
            if (route == 2) request.zooms = {zoom};
            const DeliverResult result = writeVideoWithOverlay(request);
            QVERIFY2(result.ok, qPrintable(result.error));
            QCOMPARE(streams(request.outputPath), QByteArray("video"));
        }
    }
};
QTEST_GUILESS_MAIN(TestVideoRender)
#include "test_videorender.moc"

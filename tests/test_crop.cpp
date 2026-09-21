#include <QtTest>
#include <QClipboard>
#include <QGraphicsScene>
#include <QPainter>
#include <QToolButton>
#include <QUndoStack>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QGraphicsVideoItem>
#include <QVideoSink>
#include <QVideoFrame>
#include <QMediaPlayer>
#include "videoexporter.h"
#include "canvas.h"
#include "cropbar.h"
#include "cropcontroller.h"
#include "editorwindow.h"
#include "theme.h"
#include "toolcontroller.h"
using namespace eddy;

class TestCrop : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setStyle("Fusion");
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
    }
    void handlesAndBounds() {
        for (int role = 0; role < 8; ++role) {
            CropController crop;
            crop.begin({800, 600}, {200, 150, 400, 300});
            crop.setRatio(4.0 / 3);
            const auto point = crop.handles()[role];
            crop.press(point, 1, {});
            crop.move(point + (point - crop.rect().center()) * 10, {});
            crop.release();
            QVERIFY(QRectF(0, 0, 800, 600).contains(crop.rect()));
            QVERIFY(qAbs(crop.rect().width() / crop.rect().height() - 4.0 / 3) < 0.001);
        }
    }
    void unchangedOddVideoKeepsOriginal() {
        CropController crop; crop.begin({801, 601}, {}, 2);
        QSignalSpy accepted(&crop, &CropController::accepted);
        crop.accept();
        QCOMPARE(accepted.first().first().toRect(), QRect(0, 0, 801, 601));
    }
    void moveCancelAndGrid() {
        CropController crop;
        crop.begin({801, 601}, {100, 100, 302, 202}, 2);
        crop.press({250, 200}, 1, {});
        crop.move({9999, 9999}, {});
        QCOMPARE(crop.rect().bottomRight(), QPointF(801, 601));
        const QRect output = crop.pixels();
        QVERIFY(QRect(0, 0, 801, 601).contains(output));
        QVERIFY(!(output.x() % 2 || output.y() % 2 || output.width() % 2 || output.height() % 2));
        crop.cancelDrag();
        QCOMPARE(crop.pixels(), QRect(100, 100, 302, 202));
        crop.nudge({-999, -999});
        QCOMPARE(crop.pixels(), QRect(0, 0, 302, 202));
    }
    void modifiersRebaseWithoutJump() {
        CropController crop;
        crop.begin({800, 600}, {200, 150, 400, 300});
        crop.press({600, 450}, 1, {});
        crop.move({650, 500}, {});
        const auto before = crop.rect();
        crop.move({650, 500}, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(crop.rect(), before);
        crop.move({680, 530}, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(crop.rect().center(), before.center());
        crop.cancelDrag();
        QCOMPARE(crop.rect(), QRectF(200, 150, 400, 300));
        crop.press({50, 50}, 1, {});
        crop.move({150, 100}, {});
        const auto drawn = crop.rect();
        crop.move({150, 100}, Qt::ShiftModifier);
        QCOMPARE(crop.rect(), drawn);
    }
    void cropKeepsOriginalAndOneUndo() {
        QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                image.setPixelColor(x, y, QColor(x % 256, y % 256, (x + y) % 256));
        Config cfg; cfg.animations = false; cfg.earlyExit = false;
        EditorWindow window(image, cfg, {});
        window.show(); QCoreApplication::processEvents();
        auto *button = window.findChild<QToolButton *>("crop");
        QVERIFY(button && button->isVisible() && !button->icon().isNull());
        button->click();
        auto *crop = window.findChild<CropController *>();
        auto *canvas = window.findChild<Canvas *>();
        QVERIFY(crop->active());
        crop->press({0, 0}, 1, {}); crop->move({100, 50}, {}); crop->release();
        QTest::keyClick(canvas, Qt::Key_Return);
        QVERIFY(!crop->active()); QVERIFY(window.isVisible());
        const QRect expected(100, 50, 300, 250);
        QCOMPARE(window.exportComposite(), image.copy(expected));
        window.copy(); QCOMPARE(QApplication::clipboard()->image(), image.copy(expected));
        auto *undo = window.findChild<QUndoStack *>();
        QCOMPARE(undo->count(), 1);
        undo->undo(); QCOMPARE(window.exportComposite(), image);
        undo->redo(); QCOMPARE(window.exportComposite(), image.copy(expected));
        button->click();
        QCOMPARE(crop->rect(), QRectF(expected));
        QCOMPARE(canvas->contentRect(), QRectF(image.rect()));
        crop->reset();
        QTest::keyClick(canvas, Qt::Key_Escape);
        QCOMPARE(window.exportComposite(), image.copy(expected));
        QCOMPARE(undo->count(), 1);
        QVERIFY(window.isVisible());
    }
    void escapeCancelsDragBeforeSessionAndRestoresCamera() {
        QImage image(800, 600, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, {}); window.show(); QCoreApplication::processEvents();
        auto *canvas = window.findChild<Canvas *>();
        canvas->fitMedia();
        const auto transform = canvas->transform();
        auto *crop = window.findChild<CropController *>();
        QTest::keyClick(canvas, Qt::Key_C);
        crop->press({0, 0}, 1, {}); crop->move({100, 100}, {});
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(crop->active()); QVERIFY(!crop->dragging());
        QCOMPARE(crop->rect(), QRectF(image.rect()));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!crop->active()); QVERIFY(canvas->fitted());
        QCOMPARE(canvas->transform(), transform);
        QCOMPARE(window.findChild<QUndoStack *>()->count(), 0);
    }
    void videoCoordinatesAgree_data() {
        QTest::addColumn<int>("rotation");
        QTest::addColumn<int>("sar");
        QTest::newRow("landscape") << 0 << 1;
        QTest::newRow("unsupported45") << 45 << 1;
        QTest::newRow("portrait90") << 90 << 1;
        QTest::newRow("upsideDown") << 180 << 1;
        QTest::newRow("portrait270") << 270 << 1;
        QTest::newRow("nonSquarePixels") << 0 << 2;
        QTest::newRow("rotatedNonSquarePixels") << 90 << 2;
    }
    void videoCoordinatesAgree() {
        QFETCH(int, rotation); QFETCH(int, sar);
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty()
            || QStandardPaths::findExecutable("ffprobe").isEmpty()) QSKIP("ffmpeg unavailable");
        auto run = [](const QStringList &args) {
            QProcess process;
            process.start("ffmpeg", QStringList{"-v", "error", "-y"} + args);
            return process.waitForFinished(15000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        QTemporaryDir dir;
        QImage source(160, 120, QImage::Format_RGB32);
        { QPainter p(&source);
          p.fillRect(0, 0, 80, 60, Qt::red); p.fillRect(80, 0, 80, 60, Qt::green);
          p.fillRect(0, 60, 80, 60, Qt::blue); p.fillRect(80, 60, 80, 60, Qt::yellow); }
        const auto png = dir.filePath("source.png");
        const auto base = dir.filePath("base.mp4");
        const auto input = dir.filePath("input.mp4");
        const auto output = dir.filePath("output.mp4");
        const auto decoded = dir.filePath("decoded.png");
        QVERIFY(source.save(png));
        QVERIFY(run({"-loop", "1", "-i", png, "-t", "1", "-r", "5", "-vf",
                     QString("setsar=%1").arg(sar), "-pix_fmt", "yuv420p", base}));
        QVERIFY(run({"-display_rotation", QString::number(rotation), "-i", base, "-c", "copy", input}));
        const auto loaded = loadMediaInput({InputSpec::File, input});
        QVERIFY2(loaded.ok, qPrintable(loaded.error));
        if (rotation % 90) { QVERIFY(!loaded.document.video.cropSupported); return; }
        QVERIFY(loaded.document.video.cropSupported);
        const QSize display = rotation % 180 ? QSize(120, 160 * sar) : QSize(160 * sar, 120);
        QCOMPARE(loaded.document.nativeSize(), display);
        Config cfg; cfg.animations = false; cfg.earlyExit = false;
        EditorWindow window(loaded.document, cfg, {}); window.show();
        QGraphicsVideoItem *video = nullptr;
        auto *scene = window.findChild<QGraphicsScene *>();
        QTRY_VERIFY(([&] { for (auto *item : scene->items())
            if (auto *v = dynamic_cast<QGraphicsVideoItem *>(item)) video = v;
            return video && video->videoSink()->videoFrame().isValid(); })());
        window.findChild<QMediaPlayer *>()->pause();
        window.findChild<ToolController *>()->setTool(ToolType::Crop);
        auto *crop = window.findChild<CropController *>();
        crop->press({0, 0}, 1, {}); crop->move({20, 20}, {}); crop->release();
        const auto rect = crop->pixels();
        crop->accept();
        window.copyVideoFrame();
        const QImage copied = QApplication::clipboard()->image();
        QCOMPARE(copied.size(), rect.size());
        QImage overlay(display, QImage::Format_ARGB32_Premultiplied); overlay.fill(Qt::transparent);
        VideoExportRequest request{input, output, overlay}; request.cropRect = rect;
        const auto result = writeVideoWithOverlay(request);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(run({"-i", output, "-frames:v", "1", decoded}));
        const QImage exported(decoded);
        QCOMPARE(exported.size(), rect.size());
        // Keep samples well away from chroma-subsampled color boundaries.
        for (const QPoint &sample : {QPoint(10, 10), QPoint(rect.width() - 12, 10),
                                    QPoint(10, rect.height() - 12), QPoint(rect.width() - 12, rect.height() - 12)}) {
            const QColor a = copied.pixelColor(sample), b = exported.pixelColor(sample);
            QVERIFY2(qAbs(a.red() - b.red()) < 20 && qAbs(a.green() - b.green()) < 20 && qAbs(a.blue() - b.blue()) < 20,
                     qPrintable(QString("Frame/export mismatch %1 vs %2 at %3,%4").arg(a.name(), b.name()).arg(sample.x()).arg(sample.y())));
        }
    }
    void compactBarFitsNarrowViewport() {
        CropBar bar;
        bar.setOutputSize({3840, 2160});
        bar.setAvailableWidth(280); bar.show(); QCoreApplication::processEvents();
        QVERIFY(bar.width() <= 280);
        const auto apply = bar.findChild<QToolButton *>("CropApply");
        QVERIFY(bar.rect().contains(apply->geometry()));
        QVERIFY(bar.height() > 40);
        bar.setAvailableWidth(900);
        QVERIFY(bar.height() < 40);
    }
    void toolChangeCommitsAndUnsupportedVideoExplains() {
        QImage image(400, 300, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        EditorWindow window(image, {}, {});
        auto *tools = window.findChild<ToolController *>();
        tools->setTool(ToolType::Crop);
        window.findChild<CropController *>()->setRatio(1);
        tools->setTool(ToolType::Arrow);
        QCOMPARE(tools->tool(), ToolType::Arrow);
        QCOMPARE(window.exportComposite().size(), QSize(300, 300));
        MediaDocument video; video.kind = MediaKind::Video;
        video.video = {{400, 300}, 1000, 25, false};
        EditorWindow unsupported(video, {}, {});
        unsupported.findChild<ToolController *>()->setTool(ToolType::Crop);
        QVERIFY(!unsupported.findChild<CropController *>()->active());
    }
};
QTEST_MAIN(TestCrop)
#include "test_crop.moc"

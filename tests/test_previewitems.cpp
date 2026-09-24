#include <QtTest>
#include <QGraphicsScene>
#include <QPainter>
#include <QSignalSpy>
#include <QVideoFrame>
#include <QVideoSink>
#include "previewitems.h"
using namespace eddy;

// One-pixel checkerboard: the worst case for aliasing when shrunk.
static QImage checkerboard(QSize size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) line[x] = (x + y) % 2 ? 0xffffffff : 0xff000000;
    }
    return image;
}

// Renders at `zoom` onto a device with `dpr` device pixels per logical pixel.
static QImage render(QGraphicsScene &scene, double zoom, double dpr = 1) {
    const QSize size = (scene.sceneRect().size() * zoom * dpr).toSize();
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::red);
    QPainter painter(&out);
    scene.render(&painter, QRectF(QPointF(), QSizeF(size) / dpr), scene.sceneRect(), Qt::IgnoreAspectRatio);
    painter.end();
    out.setDevicePixelRatio(1);
    return out;
}

// Two correct area filters may round an exact half differently (a 1 px
// checkerboard averages to 127.5); the aliasing this guards against differs by 20 to 100.
static constexpr double kAreaTolerance = 1.5;

static double meanDifference(const QImage &a, const QImage &b) {
    double sum = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x) {
            const QColor p = a.pixelColor(x, y), q = b.pixelColor(x, y);
            sum += qAbs(p.red() - q.red()) + qAbs(p.green() - q.green()) + qAbs(p.blue() - q.blue());
        }
    return sum / (3.0 * a.width() * a.height());
}

class TestPreviewItems : public QObject {
    Q_OBJECT
private slots:
    void shrunkImageMatchesAreaFilter_data() {
        QTest::addColumn<double>("zoom");
        QTest::addColumn<double>("dpr");
        for (double zoom : {0.25, 0.45, 0.5, 0.72, 0.9}) QTest::addRow("%d%%", int(zoom * 100)) << zoom << 1.0;
        QTest::addRow("36%% at 2x") << 0.36 << 2.0;       // shrinks by 0.72 on the device
        QTest::addRow("40%% at 1.25x") << 0.4 << 1.25;
    }
    void shrunkImageMatchesAreaFilter() {
        QFETCH(double, zoom);
        QFETCH(double, dpr);
        const QImage source = checkerboard(QSize(400, 240));
        QGraphicsScene scene(0, 0, 400, 240);
        scene.addItem(new PreviewPixmapItem(QPixmap::fromImage(source)));
        const QImage shown = render(scene, zoom, dpr);
        const QImage reference = source.scaled(shown.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        // Bilinear sampling of this pattern differs by 30-100 levels.
        QVERIFY2(meanDifference(shown, reference) < kAreaTolerance, qPrintable(QString::number(meanDifference(shown, reference))));
    }
    void actualSizeStaysPixelExact() {
        const QImage source = checkerboard(QSize(64, 48));
        QGraphicsScene scene(0, 0, 64, 48);
        scene.addItem(new PreviewPixmapItem(QPixmap::fromImage(source)));
        QCOMPARE(render(scene, 1.0), source);
    }
    void slightlyEnlargedImageBlends() {
        // Below 2x, square pixels would come out one or two device pixels wide
        // at random and bend text; blending keeps strokes even.
        QImage source(2, 1, QImage::Format_ARGB32_Premultiplied);
        source.setPixel(0, 0, 0xff000000);
        source.setPixel(1, 0, 0xffffffff);
        QGraphicsScene scene(0, 0, 2, 1);
        auto *item = new PreviewPixmapItem(QPixmap::fromImage(source));
        item->setTransformationMode(Qt::SmoothTransformation);
        scene.addItem(item);
        const int middle = qGray(render(scene, 1.5).pixel(1, 0));
        QVERIFY2(middle > 60 && middle < 195, qPrintable(QString::number(middle)));
    }
    void farEnlargedImageStaysCrisp() {
        // From 2x on every source pixel is a clean square, so small text stays readable.
        QImage source(2, 1, QImage::Format_ARGB32_Premultiplied);
        source.setPixel(0, 0, 0xff000000);
        source.setPixel(1, 0, 0xffffffff);
        QGraphicsScene scene(0, 0, 2, 1);
        auto *item = new PreviewPixmapItem(QPixmap::fromImage(source));
        item->setTransformationMode(Qt::SmoothTransformation);
        scene.addItem(item);
        const QImage shown = render(scene, 8.0);
        QCOMPARE(qGray(shown.pixel(7, 4)), 0);
        QCOMPARE(qGray(shown.pixel(8, 4)), 255);
    }
    void shrunkVideoFrameMatchesAreaFilter() {
        const QImage source = checkerboard(QSize(400, 240));
        QGraphicsScene scene(0, 0, 400, 240);
        auto *video = new PreviewVideoItem;
        video->setSize(QSizeF(400, 240));
        video->setAspectRatioMode(Qt::IgnoreAspectRatio);
        scene.addItem(video);
        video->videoSink()->setVideoFrame(QVideoFrame(source));
        QTRY_VERIFY(video->videoSink()->videoFrame().isValid());
        const QImage shown = render(scene, 0.45);
        const QImage reference = source.scaled(shown.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QVERIFY2(meanDifference(shown, reference) < kAreaTolerance, qPrintable(QString::number(meanDifference(shown, reference))));
    }

    void aBurstOfFramesStillEndsOnTheLatest() {
        // Frames arriving faster than the screen refreshes are not all converted,
        // but the newest one always gets painted.
        QGraphicsScene scene(0, 0, 40, 40);
        auto *video = new PreviewVideoItem;
        video->setSize(QSizeF(40, 40));
        video->setAspectRatioMode(Qt::IgnoreAspectRatio);
        scene.addItem(video);
        QImage red(40, 40, QImage::Format_RGB32), blue(40, 40, QImage::Format_RGB32);
        red.fill(Qt::red);
        blue.fill(Qt::blue);
        video->videoSink()->setVideoFrame(QVideoFrame(red));
        QCOMPARE(render(scene, 0.5).pixelColor(10, 10), QColor(Qt::red));
        video->videoSink()->setVideoFrame(QVideoFrame(blue));
        QCoreApplication::processEvents();
        QSignalSpy changed(&scene, &QGraphicsScene::changed);
        render(scene, 0.5);   // may still show red: too soon after the last conversion
        QTRY_VERIFY_WITH_TIMEOUT(render(scene, 0.5).pixelColor(10, 10) == QColor(Qt::blue), 500);
        // A skipped frame asks for its own repaint, so a view catches up without new frames.
        QVERIFY(changed.count() > 0 || render(scene, 0.5).pixelColor(10, 10) == QColor(Qt::blue));
    }
};
QTEST_MAIN(TestPreviewItems)
#include "test_previewitems.moc"

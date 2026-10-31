#include <QtTest>
#include <QImage>
#include <QPainter>
#include "studio/studiorenderer.h"

using namespace eddy;

// Left half red, right half blue, top-right quarter marked green.
static QImage source() {
    QImage image(200, 100, QImage::Format_RGB32);
    QPainter p(&image);
    p.fillRect(0, 0, 100, 100, Qt::red);
    p.fillRect(100, 0, 100, 100, Qt::blue);
    p.fillRect(150, 0, 50, 50, Qt::green);
    return image;
}

static bool near(QColor a, QColor b, int tolerance = 8) {
    return qAbs(a.red() - b.red()) <= tolerance && qAbs(a.green() - b.green()) <= tolerance
        && qAbs(a.blue() - b.blue()) <= tolerance;
}

static QImage draw(const StudioRenderer &renderer, const QImage &frame, const QRectF &camera) {
    QImage out(renderer.outputSize(), QImage::Format_RGB32);
    renderer.render(frame, camera, out);
    return out;
}

class TestStudioRenderer : public QObject {
    Q_OBJECT
private slots:
    void motionBlurAveragesAMovingCameraOnly() {
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), QImage());
        const QRectF still(50, 25, 100, 50);
        QImage single(renderer.outputSize(), QImage::Format_RGB32), blurred = single;
        renderer.render(source(), still, single);
        renderer.renderBlurred(source(), {still, still, still}, blurred);
        QCOMPARE(blurred, single);   // a camera at rest stays sharp
        // Sliding right, the red and blue edge smears across many pixels.
        QVector<QRectF> moving;
        for (int i = 0; i < 6; ++i) moving.append(still.translated(i * 4, 0));
        renderer.renderBlurred(source(), moving, blurred);
        auto mixed = [](const QImage &image) {
            int count = 0;
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, 20);
                count += c.red() > 30 && c.blue() > 30;
            }
            return count;
        };
        QVERIFY2(mixed(blurred) > mixed(single) + 20,
                 qPrintable(QStringLiteral("%1 vs %2").arg(mixed(blurred)).arg(mixed(single))));
    }
    void aStillCameraCopiesTheContent() {
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), QImage());
        QCOMPARE(renderer.outputSize(), QSize(200, 100));
        const QImage out = draw(renderer, source(), QRectF(0, 0, 200, 100));
        QVERIFY(near(out.pixelColor(50, 50), Qt::red));
        QVERIFY(near(out.pixelColor(120, 80), Qt::blue));
        QVERIFY(near(out.pixelColor(175, 25), Qt::green));
    }

    void theCameraMagnifiesItsWindow() {
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), QImage());
        const QImage out = draw(renderer, source(), QRectF(100, 0, 100, 50));   // the top-right quarter, 2x
        QVERIFY(near(out.pixelColor(40, 50), Qt::blue));
        QVERIFY(near(out.pixelColor(160, 50), Qt::green));
        QVERIFY(near(out.pixelColor(199, 0), Qt::green));
    }

    void aCropSetsTheOutputButNotTheCamera() {
        const StudioRenderer renderer(QSize(200, 100), QRect(100, 0, 100, 50), StudioStyle(), QImage());
        QCOMPARE(renderer.outputSize(), QSize(100, 50));
        const QImage out = draw(renderer, source(), QRectF(100, 0, 100, 50));
        QVERIFY(near(out.pixelColor(20, 25), Qt::blue));
        QVERIFY(near(out.pixelColor(80, 25), Qt::green));
    }

    void framesTheContentLikeTheFilterGraph() {
        StudioStyle style;
        style.background = StudioStyle::Background::Color;
        style.color = QColor(255, 255, 0);
        style.padding = 10;
        style.radius = 20;
        style.shadow = 0;
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), style, QImage());
        const StudioLayout layout = studioLayout(QSize(200, 100), style);
        QCOMPARE(renderer.outputSize(), layout.output);
        const QImage out = draw(renderer, source(), QRectF(0, 0, 200, 100));
        QVERIFY(near(out.pixelColor(2, 2), QColor(255, 255, 0)));
        const QPoint origin = layout.content.topLeft();
        QVERIFY(near(out.pixelColor(origin + QPoint(50, 50)), Qt::red));
        // A 20 px corner radius: the content's own corner shows the background.
        QVERIFY(near(out.pixelColor(origin + QPoint(1, 1)), QColor(255, 255, 0)));
        QVERIFY(near(out.pixelColor(origin + QPoint(20, 1)), Qt::red));
    }

    void annotationsFollowTheCameraAtTheirOwnResolution() {
        // Twice the document size: annotations stay sharp when the camera zooms.
        QImage overlay(400, 200, QImage::Format_ARGB32_Premultiplied);
        overlay.fill(Qt::transparent);
        QPainter(&overlay).fillRect(QRect(260, 20, 20, 20), Qt::white);   // document (130..140, 10..20)
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), StudioStyle(), overlay);
        const QImage still = draw(renderer, source(), QRectF(0, 0, 200, 100));
        QVERIFY(near(still.pixelColor(135, 15), Qt::white));
        const QImage zoomed = draw(renderer, source(), QRectF(100, 0, 100, 50));
        QVERIFY(near(zoomed.pixelColor(70, 30), Qt::white));    // (135 - 100) * 2, 15 * 2
        QVERIFY(near(zoomed.pixelColor(40, 30), Qt::blue));
    }

    // The frame goes over the blur's mean once (export-gpu plan E4): the same picture as the
    // mean of every camera's framed image, which is how it was drawn before.
    void aBlurredFrameMatchesTheMeanOfFramedImages() {
        StudioStyle style;
        style.background = StudioStyle::Background::Gradient;
        style.padding = 12;
        style.radius = 16;
        const StudioRenderer renderer(QSize(200, 100), QRect(0, 0, 200, 100), style, QImage());
        const QVector<QRectF> cameras = {QRectF(0, 0, 200, 100), QRectF(10, 4, 180, 90), QRectF(20, 8, 160, 80),
                                         QRectF(30, 12, 140, 70), QRectF(40, 16, 120, 60), QRectF(50, 20, 100, 50)};
        QImage blurred(renderer.outputSize(), QImage::Format_RGB32);
        renderer.renderBlurred(source(), cameras, blurred);
        QImage mean = draw(renderer, source(), cameras.first());
        {
            QPainter p(&mean);
            for (int i = 1; i < cameras.size(); ++i) {
                p.setOpacity(1.0 / (i + 1));
                p.drawImage(0, 0, draw(renderer, source(), cameras[i]));
            }
        }
        int worst = 0;
        for (int y = 0; y < mean.height(); ++y)
            for (int x = 0; x < mean.width(); ++x) {
                const QRgb a = mean.pixel(x, y), b = blurred.pixel(x, y);
                worst = std::max({worst, qAbs(qRed(a) - qRed(b)), qAbs(qGreen(a) - qGreen(b)), qAbs(qBlue(a) - qBlue(b))});
            }
        QVERIFY2(worst <= 2, qPrintable(QString::number(worst)));
    }
};

QTEST_GUILESS_MAIN(TestStudioRenderer)
#include "test_studiorenderer.moc"

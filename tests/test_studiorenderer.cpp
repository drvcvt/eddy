#include <QtTest>
#include <QImage>
#include <QPainter>
#include "studiorenderer.h"

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
};

QTEST_GUILESS_MAIN(TestStudioRenderer)
#include "test_studiorenderer.moc"

#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include "items/arrowitem.h"

using namespace eddy;

// Render a scene to an ARGB image for pixel assertions.
static QImage renderScene(QGraphicsScene &scene, QSize size) {
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    scene.render(&p, QRectF(QPointF(0,0), size), QRectF(0,0,size.width(),size.height()));
    p.end();
    return img;
}

class TestItems : public QObject {
    Q_OBJECT
private slots:
    void arrowDrawsAlongLine() {
        QGraphicsScene scene(0, 0, 100, 100);
        auto *a = new ArrowItem(QPointF(10,50), QPointF(90,50));
        a->setStrokeColor(QColor(255,0,0));
        a->setStrokeWidth(4);
        scene.addItem(a);
        QImage img = renderScene(scene, QSize(100,100));
        // A point on the shaft should be reddish (opaque).
        QColor mid = img.pixelColor(50, 50);
        QVERIFY(mid.alpha() > 200);
        QVERIFY(mid.red() > 150);
        // A far corner should be untouched (transparent).
        QVERIFY(img.pixelColor(2, 2).alpha() < 30);
    }
    void arrowBoundingRectCoversEndpoints() {
        ArrowItem a(QPointF(0,0), QPointF(40,30));
        QRectF br = a.boundingRect();
        QVERIFY(br.contains(QPointF(0,0)));
        QVERIFY(br.contains(QPointF(40,30)));
    }
};

QTEST_MAIN(TestItems)
#include "test_items.moc"

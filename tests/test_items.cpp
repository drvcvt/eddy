#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include "items/arrowitem.h"
#include "items/rectitem.h"
#include "items/ellipseitem.h"
#include "items/highlightitem.h"
#include "items/redactitem.h"
#include "items/penpathitem.h"
#include "items/textitem.h"

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
    void rectStrokesBorderNotCenter() {
        QGraphicsScene scene(0,0,100,100);
        auto *r = new RectItem(QRectF(20,20,60,60));
        r->setStrokeColor(QColor(0,128,255)); r->setStrokeWidth(4);
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(100,100));
        QVERIFY(img.pixelColor(20,50).alpha() > 150); // on the left border
        QVERIFY(img.pixelColor(50,50).alpha() < 40);  // hollow center
    }
    void redactIsOpaque() {
        QGraphicsScene scene(0,0,60,60);
        auto *r = new RedactItem(QRectF(10,10,40,40));
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(60,60));
        QColor c = img.pixelColor(30,30);
        QCOMPARE(c.alpha(), 255);
    }
    void highlightIsTranslucent() {
        QGraphicsScene scene(0,0,60,60);
        auto *h = new HighlightItem(QRectF(10,10,40,40));
        scene.addItem(h);
        QImage img = renderScene(scene, QSize(60,60));
        int a = img.pixelColor(30,30).alpha();
        QVERIFY(a > 0 && a < 200); // translucent
    }
    void penFollowsPoints() {
        QGraphicsScene scene(0,0,100,100);
        auto *pen = new PenPathItem(QPointF(10,10));
        pen->addPoint(QPointF(50,50));
        pen->addPoint(QPointF(90,10));
        pen->setStrokeColor(QColor(0,0,0)); pen->setStrokeWidth(3);
        scene.addItem(pen);
        QImage img = renderScene(scene, QSize(100,100));
        QVERIFY(img.pixelColor(50,50).alpha() > 150); // a vertex is inked
        QCOMPARE(pen->pointCount(), 3);
    }
    void textRendersInk() {
        QGraphicsScene scene(0,0,120,60);
        auto *t = new eddy::TextItem("Hi", QColor(0,0,0), 24);
        t->setPos(10,10);
        scene.addItem(t);
        QImage img = renderScene(scene, QSize(120,60));
        bool inked = false;
        for (int y=0;y<60 && !inked;++y)
            for (int x=0;x<120;++x)
                if (img.pixelColor(x,y).alpha() > 100) { inked = true; break; }
        QVERIFY(inked);
    }
};

QTEST_MAIN(TestItems)
#include "test_items.moc"

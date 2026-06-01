#include <QtTest>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QUndoStack>
#include "items/arrowitem.h"
#include "items/rectitem.h"
#include "items/ellipseitem.h"
#include "items/highlightitem.h"
#include "items/redactitem.h"
#include "items/penpathitem.h"
#include "items/textitem.h"
#include "undocommands.h"

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
    void redactBlackenIsOpaque() {
        QGraphicsScene scene(0,0,60,60);
        QImage src(60,60,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::Blacken, src, QRectF(10,10,40,40));
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(60,60));
        QColor c = img.pixelColor(30,30);
        QCOMPARE(c.alpha(), 255);
        QVERIFY(c.red() < 30);                          // near-black fill
    }
    void redactBlurObscuresSource() {
        QGraphicsScene scene(0,0,60,60);
        QImage src(60,60,QImage::Format_ARGB32);
        for (int y=0;y<60;++y) for (int x=0;x<60;++x)
            src.setPixelColor(x,y, ((x/3+y/3)%2) ? Qt::white : Qt::black);
        auto *r = new RedactItem(RedactMode::Blur, src, QRectF(10,10,40,40));
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(60,60));
        QColor a = img.pixelColor(30,30), b = img.pixelColor(31,30);
        QVERIFY(a.alpha() > 0);                         // something painted
        QVERIFY(qAbs(a.red()-b.red()) < 70);            // neighbours similar (blurred, no sharp edge)
    }
    void redactResizeUpdatesRegion() {
        QImage src(60,60,QImage::Format_ARGB32); src.fill(Qt::white);
        RedactItem r(RedactMode::Blacken, src, QRectF(0,0,10,10));
        QCOMPARE(r.rect(), QRectF(0,0,10,10));
        r.setRect(QRectF(5,5,30,20));
        QCOMPARE(r.rect(), QRectF(5,5,30,20));
    }
    void redactSitsBelowAnnotations() {
        QGraphicsScene scene(0,0,100,100);
        auto *arrow = new ArrowItem(QPointF(10,50), QPointF(90,50));
        arrow->setStrokeColor(QColor(255,0,0)); arrow->setStrokeWidth(8);
        scene.addItem(arrow);                                  // default zValue 0
        QImage src(100,100,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::Blacken, src, QRectF(20,20,60,60));
        scene.addItem(r);                                      // zValue -500 -> below the arrow
        QImage img = renderScene(scene, QSize(100,100));
        QColor mid = img.pixelColor(50,50);                    // on the arrow shaft, inside the redact
        QVERIFY(mid.red() > 150 && mid.green() < 100);         // arrow still visible (not blacked out)
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
    void arrowSetStartMovesStart() {
        ArrowItem a({0,0}, {10,10});
        a.setStart({3,4});
        QCOMPARE(a.start(), QPointF(3,4));
        QCOMPARE(a.end(), QPointF(10,10));
    }
    void resizeRectCommandUndoRedo() {
        RectItem *r = new RectItem(QRectF(0,0,10,10));
        QUndoStack undo;
        undo.push(new ResizeRectCommand(r, QRectF(0,0,10,10), QRectF(0,0,40,30)));
        QCOMPARE(r->rect(), QRectF(0,0,40,30));
        undo.undo();
        QCOMPARE(r->rect(), QRectF(0,0,10,10));
        delete r;
    }
    void ocrBlackenCoversOnlyTextRects() {
        QGraphicsScene scene(0,0,80,80);
        QImage src(80,80,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::OcrBlacken, src, QRectF(10,10,60,60));
        r->setTextRects({ QRectF(20,20,30,10) });
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(80,80));
        QCOMPARE(img.pixelColor(30,24).alpha(), 255);     // inside the text rect -> covered
        QVERIFY(img.pixelColor(30,24).red() < 30);         // near-black
        QCOMPARE(img.pixelColor(62,62).alpha(), 0);        // inside region, outside text rect -> untouched
    }
    void ocrBlurPaintsOnlyTextRects() {
        QGraphicsScene scene(0,0,80,80);
        QImage src(80,80,QImage::Format_ARGB32);
        for (int y=0;y<80;++y) for (int x=0;x<80;++x)
            src.setPixelColor(x,y, ((x/3+y/3)%2) ? Qt::white : Qt::black);
        auto *r = new RedactItem(RedactMode::OcrBlur, src, QRectF(10,10,60,60));
        r->setTextRects({ QRectF(20,20,30,12) });
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(80,80));
        QVERIFY(img.pixelColor(30,24).alpha() > 0);        // inside the text rect -> blurred content
        QCOMPARE(img.pixelColor(62,62).alpha(), 0);        // inside region, outside text rect -> untouched
    }
    void redactDetectingCoversWholeRegionThenNarrows() {
        QGraphicsScene scene(0,0,80,80);
        QImage src(80,80,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::OcrBlacken, src, QRectF(10,10,60,60));
        r->setTextRects({ QRectF(20,20,30,10) });
        r->setDetecting(true);                              // pending → cover whole region (no leak)
        scene.addItem(r);
        QImage img = renderScene(scene, QSize(80,80));
        QVERIFY(r->isDetecting());
        QCOMPARE(img.pixelColor(62,62).alpha(), 255);       // outside text rect, still covered while detecting
        r->setDetecting(false);                             // done → narrow to text rects
        QImage img2 = renderScene(scene, QSize(80,80));
        QVERIFY(!r->isDetecting());
        QCOMPARE(img2.pixelColor(62,62).alpha(), 0);        // now uncovered (only the text rect is)
    }
    void setRedactModeCommandUndoRedo() {
        QImage src(40,40,QImage::Format_ARGB32); src.fill(Qt::white);
        RedactItem *r = new RedactItem(RedactMode::Blacken, src, QRectF(0,0,20,20));
        QUndoStack undo;
        undo.push(new SetRedactModeCommand(r, RedactMode::Blacken, RedactMode::Blur));
        QCOMPARE(r->mode(), RedactMode::Blur);
        undo.undo();
        QCOMPARE(r->mode(), RedactMode::Blacken);
        undo.redo();
        QCOMPARE(r->mode(), RedactMode::Blur);
        delete r;
    }
    void redactBlurResamplesOnMove() {
        QImage src(80,40,QImage::Format_ARGB32);
        for (int y=0;y<40;++y) for (int x=0;x<80;++x)
            src.setPixelColor(x,y, x < 40 ? Qt::black : Qt::white);
        QGraphicsScene scene(0,0,80,40);
        auto *r = new RedactItem(RedactMode::Blur, src, QRectF(0,0,20,20));   // over the black half
        scene.addItem(r);
        QImage a = renderScene(scene, QSize(80,40));
        QVERIFY(a.pixelColor(10,10).red() < 80);          // blurred black -> dark
        r->setPos(50, 0);                                  // move the cover over the white half
        QImage b = renderScene(scene, QSize(80,40));
        QVERIFY(b.pixelColor(60,10).red() > 175);          // re-sampled -> blurred white -> light
    }
    void ocrRedactResizeCoversWholeRegionUntilRedetect() {
        QImage src(80,80,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::OcrBlacken, src, QRectF(10,10,40,40));
        r->setTextRects({ QRectF(20,20,20,8) });
        r->setDetecting(false);                       // had a prior result (narrowed)
        r->setRect(QRectF(10,10,60,60));              // user resizes
        QVERIFY(r->isDetecting());                    // now covers whole region again
        delete r;
    }
    void ocrRedactCoversWholeOnMove() {
        QGraphicsScene scene(0,0,200,200);            // needs a scene for ItemPositionHasChanged
        QImage src(200,200,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::OcrBlacken, src, QRectF(10,10,40,40));
        r->setTextRects({ QRectF(20,20,20,8) }); r->setDetecting(false);
        scene.addItem(r);
        r->setPos(60, 0);                             // user drags the body
        QVERIFY(r->isDetecting());                    // moved OCR redact covers whole (no stale exposure)
    }
};

QTEST_MAIN(TestItems)
#include "test_items.moc"

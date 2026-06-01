#include <QtTest>
#include <QCoreApplication>
#include <QGraphicsScene>
#include "selectionhandles.h"
#include "items/rectitem.h"
#include "items/redactitem.h"
#include <QImage>
using namespace eddy;
class TestSelectionHandles : public QObject {
    Q_OBJECT
private slots:
    void showsHandlesForSelectedRect() {
        QGraphicsScene scene(0,0,200,200);
        auto *r = new RectItem(QRectF(10,10,50,40));
        r->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene.addItem(r);
        SelectionHandles handles(&scene);
        r->setSelected(true);
        QCOMPARE(handles.handleCount(), 8);            // rect → 8 corner/edge handles
        r->setSelected(false);
        QCOMPARE(handles.handleCount(), 0);            // none when nothing selected
    }
    void showsHandlesForSelectedRedact() {
        QGraphicsScene scene(0,0,200,200);
        QImage src(200,200,QImage::Format_ARGB32); src.fill(Qt::white);
        auto *r = new RedactItem(RedactMode::Blacken, src, QRectF(10,10,50,40));
        r->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene.addItem(r);
        SelectionHandles handles(&scene);
        r->setSelected(true);
        QCOMPARE(handles.handleCount(), 8);            // redact → 8 corner/edge handles
        r->setSelected(false);
        QCOMPARE(handles.handleCount(), 0);
    }
    void handlesFollowItemMove() {
        QGraphicsScene scene(0,0,200,200);
        auto *r = new RectItem(QRectF(10,10,50,40));
        r->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene.addItem(r);
        SelectionHandles handles(&scene);
        r->setSelected(true);
        const QPointF before = handles.handleScenePos(0);   // top-left handle
        r->moveBy(30, 20);                                   // move the item body
        QCoreApplication::processEvents();                  // let scene::changed -> reposition fire
        const QPointF after = handles.handleScenePos(0);
        QCOMPARE(after, before + QPointF(30, 20));            // handle tracked the move
    }
};
QTEST_MAIN(TestSelectionHandles)
#include "test_selectionhandles.moc"

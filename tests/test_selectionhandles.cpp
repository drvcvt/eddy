#include <QtTest>
#include <QGraphicsScene>
#include "selectionhandles.h"
#include "items/rectitem.h"
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
};
QTEST_MAIN(TestSelectionHandles)
#include "test_selectionhandles.moc"

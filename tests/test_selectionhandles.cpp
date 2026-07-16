#include <QtTest>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QUndoStack>
#include "selectionhandles.h"
#include "items/rectitem.h"
#include "items/redactitem.h"
#include "items/textitem.h"
#include <QImage>
using namespace eddy;
class TestSelectionHandles : public QObject {
    Q_OBJECT
private slots:
    void shiftResizePreservesOriginalAspectRatio() {
        const QRectF before(10,20,80,40);
        QCOMPARE(resizedRect(before, 4, QPointF(130,90), Qt::ShiftModifier),
                 QRectF(10,20,120,60));
    }
    void altResizeMirrorsAroundOriginalCenter() {
        const QRectF before(10,20,80,40);
        QCOMPARE(resizedRect(before, 4, QPointF(110,80), Qt::AltModifier),
                 QRectF(-10,0,120,80));
    }
    void shiftArrowResizeSnapsToFortyFiveDegreeIncrement() {
        const QPointF end = snappedArrowEnd({0,0}, {20,13}, Qt::ShiftModifier);
        QVERIFY(qAbs(end.x() - end.y()) < 0.001);
    }
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
    void selectedTextGetsOneWidthHandle() {
        QGraphicsScene scene(0,0,300,200);
        QUndoStack undo;
        auto *text = new TextItem(QStringLiteral("A long text label"), Qt::red, 18);
        text->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene.addItem(text);
        SelectionHandles handles(&scene, &undo);
        text->setSelected(true);
        QCOMPARE(text->textWidth(), -1.0);
        QCOMPARE(handles.handleCount(), 1);
        QVERIFY(handles.handleScenePos(0).x() > text->sceneBoundingRect().center().x());
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

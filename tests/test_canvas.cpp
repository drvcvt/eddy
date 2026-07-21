#include <QtTest>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QUndoStack>
#include <QSignalSpy>
#include <QScrollBar>
#include "canvas.h"
#include "toolcontroller.h"
#include "items/rectitem.h"
#include "items/textitem.h"
using namespace eddy;
class TestCanvas : public QObject {
    Q_OBJECT
private slots:
    void constructsAndShows() {
        QGraphicsScene scene(0,0,50,50);
        QUndoStack undo;
        ToolController tc(&scene,&undo,QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        Canvas c(&scene, &tc);
        c.resize(60,60);
        QCOMPARE(c.zoom(), 1.0);
        QVERIFY(c.scene() == &scene);
    }
    void doesNotCacheTheSolidBackground() {
        QGraphicsScene scene(0,0,50,50);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        QCOMPARE(canvas.cacheMode(), QGraphicsView::CacheNone);
    }
    void resizeEmitsViewChanged() {
        QGraphicsScene scene(0,0,100,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.show();                       // offscreen platform; ensures resizeEvent is delivered
        QSignalSpy spy(&canvas, &Canvas::viewChanged);
        canvas.resize(320, 240);
        QVERIFY(spy.count() >= 1);
    }
    void middleDragPansView() {
        QGraphicsScene scene(0,0,3000,3000);     // far larger than the viewport -> scroll range
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(10,10,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.resize(200,200);
        canvas.show();
        canvas.horizontalScrollBar()->setValue(800);
        canvas.verticalScrollBar()->setValue(800);
        const int hx = canvas.horizontalScrollBar()->value();
        const int vy = canvas.verticalScrollBar()->value();
        QTest::mousePress(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, QPoint(150,150));
        QTest::mouseMove(canvas.viewport(), QPoint(110,120));   // drag up-left by (40,30)
        QTest::mouseRelease(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, QPoint(110,120));
        QCOMPARE(canvas.horizontalScrollBar()->value(), hx + 40);   // dragging left scrolls right
        QCOMPARE(canvas.verticalScrollBar()->value(), vy + 30);
    }
    void spaceDragPansView() {
        QGraphicsScene scene(0,0,3000,3000);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(10,10,QImage::Format_ARGB32_Premultiplied));
        tools.setTool(ToolType::Move);
        Canvas canvas(&scene, &tools);
        canvas.resize(200,200);
        canvas.show();
        canvas.horizontalScrollBar()->setValue(800);
        canvas.verticalScrollBar()->setValue(800);
        const int hx = canvas.horizontalScrollBar()->value();
        const int vy = canvas.verticalScrollBar()->value();

        canvas.setSpacePan(true);
        QVERIFY(canvas.spacePanActive());
        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(150,150));
        QTest::mouseMove(canvas.viewport(), QPoint(110,120));
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(110,120));
        canvas.setSpacePan(false);

        QCOMPARE(canvas.horizontalScrollBar()->value(), hx + 40);
        QCOMPARE(canvas.verticalScrollBar()->value(), vy + 30);
        QVERIFY(!canvas.spacePanActive());
    }
    void zoomCommandsClampResetAndFit() {
        QGraphicsScene scene(0,0,1000,500);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(10,10,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.setAnimationsEnabled(false);
        canvas.resize(200,200);
        canvas.show();

        canvas.zoomBy(2.0);
        QCOMPARE(canvas.zoom(), 2.0);
        canvas.resetZoom();
        QCOMPARE(canvas.zoom(), 1.0);
        canvas.fitMedia();
        QVERIFY(canvas.zoom() > 0.05);
        QVERIFY(canvas.zoom() < 1.0);
    }
    void cursorMatchesActiveInteraction() {
        QGraphicsScene scene(0,0,100,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        tools.setTool(ToolType::Rect);
        QCOMPARE(canvas.viewport()->cursor().shape(), Qt::CrossCursor);
        tools.setTool(ToolType::Text);
        QCOMPARE(canvas.viewport()->cursor().shape(), Qt::IBeamCursor);
        canvas.setSpacePan(true);
        QCOMPARE(canvas.viewport()->cursor().shape(), Qt::OpenHandCursor);
    }
    void shiftClickAddsToSelection() {
        QGraphicsScene scene(0,0,200,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(200,100,QImage::Format_ARGB32_Premultiplied));
        tools.setTool(ToolType::Move);
        Canvas canvas(&scene, &tools);
        canvas.resize(220,120);
        canvas.show();
        auto *a = new RectItem(QRectF(10,10,30,30));
        auto *b = new RectItem(QRectF(80,10,30,30));
        for (QGraphicsItem *item : {static_cast<QGraphicsItem *>(a), static_cast<QGraphicsItem *>(b)}) {
            item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            scene.addItem(item);
        }

        QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier,
                          canvas.mapFromScene(a->sceneBoundingRect().center()));
        QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::ShiftModifier,
                          canvas.mapFromScene(b->sceneBoundingRect().center()));
        QCOMPARE(scene.selectedItems().size(), 2);
    }
    void altDragDuplicatesInOneUndoStep() {
        QGraphicsScene scene(0,0,200,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(200,100,QImage::Format_ARGB32_Premultiplied));
        tools.setTool(ToolType::Move);
        Canvas canvas(&scene, &tools);
        canvas.resize(220,120);
        canvas.show();
        auto *item = new RectItem(QRectF(20,20,30,30));
        item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene.addItem(item);
        const QPoint start = canvas.mapFromScene(item->sceneBoundingRect().center());
        const QPoint end = start + QPoint(25, 12);

        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::AltModifier, start);
        QTest::mouseMove(canvas.viewport(), end);
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::AltModifier, end);

        QCOMPARE(scene.items().size(), 2);
        QCOMPARE(undo.count(), 1);
        QVERIFY(scene.selectedItems().first()->pos() != QPointF());
        undo.undo();
        QCOMPARE(scene.items().size(), 1);
    }
    void eyedropperTogglesActiveState() {
        QGraphicsScene scene(0,0,100,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.resize(120,120); canvas.show();
        QVERIFY(!canvas.eyedropperActive());
        canvas.startEyedropper();
        QVERIFY(canvas.eyedropperActive());
        canvas.cancelEyedropper();
        QVERIFY(!canvas.eyedropperActive());
    }
    void eyedropperLeftClickPicksAndExits() {
        QGraphicsScene scene(0,0,100,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tools.setTool(ToolType::Move);            // release after the pick falls through natively
        Canvas canvas(&scene, &tools);
        canvas.resize(120,120); canvas.show();
        QSignalSpy spy(&canvas, &Canvas::colorPicked);
        canvas.startEyedropper();
        QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(40,40));
        QCOMPARE(spy.count(), 1);                 // one colour picked
        QVERIFY(!canvas.eyedropperActive());      // pick exits the mode
    }
    void eyedropperRightClickCancels() {
        QGraphicsScene scene(0,0,100,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        Canvas canvas(&scene, &tools);
        canvas.resize(120,120); canvas.show();
        QSignalSpy spy(&canvas, &Canvas::colorPicked);
        canvas.startEyedropper();
        QTest::mouseClick(canvas.viewport(), Qt::RightButton, Qt::NoModifier, QPoint(40,40));
        QCOMPARE(spy.count(), 0);                 // cancel picks nothing
        QVERIFY(!canvas.eyedropperActive());
    }
    void movingAnnotationCreatesOneUndoStep() {
        QGraphicsScene scene(0,0,100,100);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tools.setTool(ToolType::Move);
        Canvas canvas(&scene, &tools);
        canvas.resize(120,120);
        canvas.show();

        auto *item = scene.addRect(QRectF(20,20,20,20));
        item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        const QPointF before = item->pos();
        const QPoint start = canvas.mapFromScene(item->sceneBoundingRect().center());
        const QPoint end = start + QPoint(18, 11);

        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas.viewport(), end);
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, end);

        QVERIFY(item->pos() != before);
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(item->pos(), before);
    }
    void textToolDragsExistingTextInsteadOfCreatingAnother() {
        QGraphicsScene scene(0,0,240,140);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(240,140,QImage::Format_ARGB32_Premultiplied));
        tools.setTool(ToolType::Text);
        Canvas canvas(&scene, &tools);
        canvas.resize(260,160);
        canvas.show();
        auto *text = new TextItem(QStringLiteral("Move me"), Qt::red, 18);
        text->setPos(40,40);
        text->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene.addItem(text);
        const QPointF before = text->pos();
        const QPoint start = canvas.mapFromScene(text->sceneBoundingRect().center());
        const QPoint end = start + QPoint(24, 12);

        QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(canvas.viewport(), end);
        QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, end);

        QCOMPARE(scene.items().size(), 1);
        QCOMPARE(text->pos(), before + QPointF(24,12));
        QCOMPARE(undo.count(), 1);
    }
};
QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"

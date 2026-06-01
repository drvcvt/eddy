#include <QtTest>
#include <QGraphicsScene>
#include <QUndoStack>
#include <QSignalSpy>
#include <QScrollBar>
#include "canvas.h"
#include "toolcontroller.h"
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
};
QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"

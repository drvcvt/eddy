#include <QtTest>
#include <QGraphicsScene>
#include <QUndoStack>
#include <QSignalSpy>
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
};
QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"

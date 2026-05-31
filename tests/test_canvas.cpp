#include <QtTest>
#include <QGraphicsScene>
#include <QUndoStack>
#include "canvas.h"
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
};
QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"

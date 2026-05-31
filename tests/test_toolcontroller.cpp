#include <QtTest>
#include <QSignalSpy>
#include <QGraphicsScene>
#include <QUndoStack>
#include "toolcontroller.h"
#include "items/arrowitem.h"
#include "items/rectitem.h"
#include "items/rasteritem.h"
#include "items/textitem.h"

using namespace eddy;

class TestToolController : public QObject {
    Q_OBJECT
private slots:
    void arrowDragAddsItem() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tc.setTool(ToolType::Arrow);
        tc.begin({10,10}); tc.update({40,40}); tc.finish({80,80});
        QCOMPARE(scene.items().size(), 1);
        auto *a = dynamic_cast<ArrowItem*>(scene.items().first());
        QVERIFY(a);
        QCOMPARE(a->end(), QPointF(80,80));
    }
    void undoRemovesItem() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        tc.setTool(ToolType::Rect);
        tc.begin({5,5}); tc.finish({25,25});
        QCOMPARE(scene.items().size(), 1);
        undo.undo();
        QCOMPARE(scene.items().size(), 0);
        undo.redo();
        QCOMPARE(scene.items().size(), 1);
    }
    void toolFromNameMaps() {
        QCOMPARE(toolFromName("blur"), ToolType::Blur);
        QCOMPARE(toolFromName("nonsense"), ToolType::Arrow); // fallback
        QCOMPARE(toolFromName("box"), ToolType::Rect);
        QCOMPARE(toolFromName("rectangle"), ToolType::Rect);
        QCOMPARE(toolFromName("circle"), ToolType::Ellipse);
        QCOMPARE(toolFromName("redact"), ToolType::Redact);
        QCOMPARE(toolFromName("pixelate"), ToolType::Pixelate);
        QCOMPARE(toolFromName("move"), ToolType::Move);
    }
    void shapeMovableRasterNot() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(60,60,QImage::Format_ARGB32_Premultiplied));
        tc.setTool(ToolType::Arrow);
        tc.begin({1,1}); tc.finish({20,20});
        auto *arrow = scene.items().first();
        QVERIFY(arrow->flags() & QGraphicsItem::ItemIsMovable);
        QVERIFY(arrow->flags() & QGraphicsItem::ItemIsSelectable);
        tc.setTool(ToolType::Blur);
        tc.begin({5,5}); tc.finish({25,25});
        bool sawRaster = false;
        for (auto *it : scene.items()) {
            if (auto *ri = dynamic_cast<RasterItem*>(it)) {
                sawRaster = true;
                QVERIFY(!(ri->flags() & QGraphicsItem::ItemIsMovable));
            }
        }
        QVERIFY(sawRaster);
    }
    void placeTextAddsEditableItem() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        auto *t = tc.placeText({10,10});
        QVERIFY(t);
        QVERIFY(scene.items().contains(t));
        QVERIFY(dynamic_cast<TextItem*>(t));
        QVERIFY(t->flags() & QGraphicsItem::ItemIsMovable);
        QVERIFY(t->flags() & QGraphicsItem::ItemIsSelectable);
        undo.undo();
        QVERIFY(!scene.items().contains(t));
    }
    void toolChangedEmittedOnlyOnChange() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(10,10,QImage::Format_ARGB32_Premultiplied));
        QSignalSpy spy(&tc, &ToolController::toolChanged);
        tc.setTool(ToolType::Rect);
        tc.setTool(ToolType::Rect);   // no-op, no emit
        tc.setTool(ToolType::Pen);
        QCOMPARE(spy.count(), 2);
    }
    void committedItemOpaqueWhenAnimationsOff() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(20,20,QImage::Format_ARGB32_Premultiplied));
        tc.setAnimationsEnabled(false);
        tc.setTool(ToolType::Arrow);
        tc.begin({1,1}); tc.finish({10,10});
        QCOMPARE(scene.items().first()->opacity(), 1.0);
    }
    void priorFadeSettledOnNextCommit() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(30,30,QImage::Format_ARGB32_Premultiplied));
        tc.setAnimationsEnabled(true);
        tc.setTool(ToolType::Arrow);
        tc.begin({1,1}); tc.finish({5,5});      // item A: fade started (opacity ~0)
        tc.begin({6,6}); tc.finish({10,10});    // item B commit -> finalizeFade snaps A to 1.0
        auto items = scene.items();             // top-first: [B, A]
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.last()->opacity(), 1.0); // A (first committed) settled to fully opaque
    }
};

QTEST_MAIN(TestToolController)
#include "test_toolcontroller.moc"

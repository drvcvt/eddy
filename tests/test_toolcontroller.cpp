#include <QtTest>
#include <QSignalSpy>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QPainter>
#include <QUndoStack>
#include "toolcontroller.h"
#include "undocommands.h"
#include "items/arrowitem.h"
#include "items/ellipseitem.h"
#include "items/rectitem.h"
#include "items/redactitem.h"
#include "items/textitem.h"
#include "items/spotlightitem.h"

using namespace eddy;

static QImage renderScene(QGraphicsScene &scene, const QSize &size) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    scene.render(&painter, QRectF(QPointF(), size), QRectF(QPointF(), size));
    return image;
}

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
    void shiftRectCreatesSquare() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tc.setAnimationsEnabled(false);
        tc.setTool(ToolType::Rect);
        tc.begin({10,10});
        tc.finish({40,25}, Qt::ShiftModifier);
        auto *rect = dynamic_cast<RectItem *>(scene.items().first());
        QVERIFY(rect);
        QCOMPARE(rect->rect(), QRectF(10,10,30,30));
    }
    void shiftArrowSnapsToFortyFiveDegreeIncrement() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tc.setAnimationsEnabled(false);
        tc.setTool(ToolType::Arrow);
        tc.begin({0,0});
        tc.finish({30,10}, Qt::ShiftModifier);
        auto *arrow = dynamic_cast<ArrowItem *>(scene.items().first());
        QVERIFY(arrow);
        QVERIFY(qAbs(arrow->end().y()) < 0.001);
    }
    void altEllipseCreatesFromCenter() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tc.setAnimationsEnabled(false);
        tc.setTool(ToolType::Ellipse);
        tc.begin({50,50});
        tc.finish({70,60}, Qt::AltModifier);
        auto *ellipse = dynamic_cast<EllipseItem *>(scene.items().first());
        QVERIFY(ellipse);
        QCOMPARE(ellipse->rect(), QRectF(30,40,40,20));
    }
    void cancelActiveDiscardsPreviewWithoutUndo() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tc.setTool(ToolType::Rect);
        tc.begin({10,10});
        QCOMPARE(scene.items().size(), 1);
        QVERIFY(tc.cancelActive());
        QCOMPARE(scene.items().size(), 0);
        QCOMPARE(undo.count(), 0);
        QVERIFY(!tc.cancelActive());
    }
    void duplicateSelectionIsOneUndoStep() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        auto *rect = new RectItem(QRectF(0,0,20,10));
        rect->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        rect->setPos(12, 18);
        rect->setSelected(true);
        scene.addItem(rect);

        QVERIFY(tc.duplicateSelection(QPointF(8,8)));
        QCOMPARE(scene.items().size(), 2);
        QCOMPARE(undo.count(), 1);
        const auto selected = scene.selectedItems();
        QCOMPARE(selected.size(), 1);
        auto *copy = dynamic_cast<RectItem *>(selected.first());
        QVERIFY(copy);
        QVERIFY(copy != rect);
        QCOMPARE(copy->rect(), rect->rect());
        QCOMPARE(copy->pos(), QPointF(20,26));

        undo.undo();
        QCOMPARE(scene.items().size(), 1);
        undo.redo();
        QCOMPARE(scene.items().size(), 2);
    }
    void nudgeSelectionIsOneUndoStep() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        auto *a = new RectItem(QRectF(0,0,10,10));
        auto *b = new EllipseItem(QRectF(20,20,10,10));
        for (QGraphicsItem *item : {static_cast<QGraphicsItem *>(a), static_cast<QGraphicsItem *>(b)}) {
            item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            scene.addItem(item);
            item->setSelected(true);
        }

        QVERIFY(tc.nudgeSelection(QPointF(10,-10)));
        QCOMPARE(a->pos(), QPointF(10,-10));
        QCOMPARE(b->pos(), QPointF(10,-10));
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(a->pos(), QPointF());
        QCOMPARE(b->pos(), QPointF());
    }
    void duplicateMoveIsOneUndoStep() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        auto *rect = new RectItem(QRectF(0,0,20,10));
        rect->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene.addItem(rect);
        rect->setSelected(true);

        QVERIFY(tc.beginDuplicateMove());
        QCOMPARE(scene.items().size(), 2);
        auto *copy = scene.selectedItems().first();
        copy->setPos(14, 9);
        tc.finishMove();
        QCOMPARE(undo.count(), 1);

        undo.undo();
        QCOMPARE(scene.items().size(), 1);
        QCOMPARE(rect->pos(), QPointF());
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
        QCOMPARE(toolFromName("blur"), ToolType::Redact);      // legacy alias
        QCOMPARE(toolFromName("pixelate"), ToolType::Redact);  // legacy alias
        QCOMPARE(toolFromName("nonsense"), ToolType::Arrow); // fallback
        QCOMPARE(toolFromName("box"), ToolType::Rect);
        QCOMPARE(toolFromName("rectangle"), ToolType::Rect);
        QCOMPARE(toolFromName("circle"), ToolType::Ellipse);
        QCOMPARE(toolFromName("redact"), ToolType::Redact);
        QCOMPARE(toolFromName("move"), ToolType::Move);
    }
    void redactToolCreatesMovableBlurRedact() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(60,60,QImage::Format_ARGB32_Premultiplied));
        tc.setTool(ToolType::Redact);
        tc.begin({5,5}); tc.finish({25,25});
        auto *r = dynamic_cast<RedactItem*>(scene.items().first());
        QVERIFY(r);
        QCOMPARE(r->mode(), RedactMode::Blur);                 // default redact mode is Blur
        QVERIFY(r->flags() & QGraphicsItem::ItemIsMovable);
        QVERIFY(r->flags() & QGraphicsItem::ItemIsSelectable);
    }
    void updatedBackgroundFeedsNewRedactItems() {
        QGraphicsScene scene(0, 0, 40, 40);
        QUndoStack undo;
        QImage black(40, 40, QImage::Format_ARGB32_Premultiplied); black.fill(Qt::black);
        QImage white(40, 40, QImage::Format_ARGB32_Premultiplied); white.fill(Qt::white);
        ToolController controller(&scene, &undo, black);
        controller.setAnimationsEnabled(false);
        controller.setBackground(white);
        controller.setTool(ToolType::Redact);
        controller.begin({0, 0});
        controller.finish({40, 40});

        QVERIFY(renderScene(scene, QSize(40, 40)).pixelColor(20, 20).red() > 225);
    }
    void newTextCommitsAsOneUndoStep() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        auto *t = dynamic_cast<TextItem *>(tc.placeText({10,10}));
        QVERIFY(t);
        QVERIFY(scene.items().contains(t));
        QVERIFY(t->flags() & QGraphicsItem::ItemIsMovable);
        QVERIFY(t->flags() & QGraphicsItem::ItemIsSelectable);
        QCOMPARE(undo.count(), 0);
        t->setPlainText(QStringLiteral("hello\nworld"));
        QVERIFY(tc.commitTextEdit());
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QVERIFY(!scene.items().contains(t));
    }
    void escapeCancelsNewTextWithoutUndo() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        tc.placeText({10,10});
        QVERIFY(tc.cancelTextEdit());
        QCOMPARE(scene.items().size(), 0);
        QCOMPARE(undo.count(), 0);
    }
    void existingTextEditCommitsOrRestoresAsOneTransaction() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        auto *text = new TextItem(QStringLiteral("before"), Qt::red, 18);
        text->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene.addItem(text);

        tc.editText(text);
        text->setPlainText(QStringLiteral("temporary"));
        QVERIFY(tc.cancelTextEdit());
        QCOMPARE(text->toPlainText(), QStringLiteral("before"));
        QCOMPARE(undo.count(), 0);

        tc.editText(text);
        text->setPlainText(QStringLiteral("after\nline"));
        QVERIFY(tc.commitTextEdit());
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(text->toPlainText(), QStringLiteral("before"));
        undo.redo();
        QCOMPARE(text->toPlainText(), QStringLiteral("after\nline"));
    }
    void newSpotlightReplacesOldInOneUndoStep() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
        tc.setAnimationsEnabled(false); tc.setTool(ToolType::Spotlight);
        tc.begin({10,10}); tc.finish({40,40});
        QCOMPARE(tc.tool(), ToolType::Move);
        QCOMPARE(scene.selectedItems().size(), 1);
        tc.setTool(ToolType::Spotlight);
        tc.begin({50,50}); tc.finish({90,90});
        QCOMPARE(scene.items().size(), 1);
        QCOMPARE(undo.count(), 2);
        QCOMPARE(dynamic_cast<SpotlightItem *>(scene.items().first())->rect(), QRectF(50,50,40,40));
        undo.undo();
        QCOMPARE(dynamic_cast<SpotlightItem *>(scene.items().first())->rect(), QRectF(10,10,30,30));
    }
    void placeTextUsesConfiguredFont() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(50,50,QImage::Format_ARGB32_Premultiplied));
        tc.setTextFont(QStringLiteral("DejaVu Sans Mono"));
        auto *t = dynamic_cast<TextItem *>(tc.placeText({10,10}));
        QVERIFY(t);
        QCOMPARE(t->font().family(), QStringLiteral("DejaVu Sans Mono"));
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
    void redactDrawSelectsAndSwitchesToMove() {
        QGraphicsScene scene; QUndoStack undo;
        ToolController tc(&scene, &undo, QImage(60,60,QImage::Format_ARGB32_Premultiplied));
        // a previously-selected item must not remain selected after drawing a new redact
        auto *prior = new RectItem(QRectF(0,0,10,10));
        prior->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene.addItem(prior);
        prior->setSelected(true);
        tc.setTool(ToolType::Redact);
        tc.begin({5,5}); tc.finish({40,40});
        QCOMPARE(tc.tool(), ToolType::Move);                       // auto-switched to Move
        const auto sel = scene.selectedItems();
        QCOMPARE(sel.size(), 1);                                   // sole selection
        QVERIFY(dynamic_cast<RedactItem*>(sel.first()));           // and it is the new redact
    }
    void removeItemCommandUndoRedo() {
        QGraphicsScene scene; QUndoStack undo;
        auto *r = new QGraphicsRectItem(0,0,10,10);
        scene.addItem(r);
        QCOMPARE(scene.items().size(), 1);
        undo.push(new RemoveItemCommand(&scene, r));   // redo() removes
        QCOMPARE(scene.items().size(), 0);
        undo.undo();                                   // re-adds
        QCOMPARE(scene.items().size(), 1);
        undo.redo();
        QCOMPARE(scene.items().size(), 0);
    }
};

QTEST_MAIN(TestToolController)
#include "test_toolcontroller.moc"

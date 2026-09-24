#include <QtTest>
#include <QGraphicsScene>
#include <QUndoStack>
#include "canvas.h"
#include "snapping.h"
#include "toolcontroller.h"
#include "items/rectitem.h"
#include "items/spotlightitem.h"

using namespace eddy;

static void moveTo(QWidget *viewport, QPoint p, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent e(QEvent::MouseMove, QPointF(p), QPointF(viewport->mapToGlobal(p)), Qt::NoButton, Qt::LeftButton,
                  modifiers);
    QApplication::sendEvent(viewport, &e);
}

static RectItem *box(QGraphicsScene &scene, QPointF at) {
    auto *item = new RectItem(QRectF(0, 0, 40, 40));
    item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    item->setPos(at);
    scene.addItem(item);
    return item;
}

class TestSnapping : public QObject {
    Q_OBJECT
private slots:
    void catchesAtFiveAndLetsGoAtEight() {
        Snapper snapper;
        const QVector<QRectF> targets = {QRectF(100, 300, 50, 50)};
        // 4 off the target's left edge: caught.
        QCOMPARE(snapper.snap(QRectF(104, 0, 10, 10), targets, 5, 8), QPointF(-4, 0));
        QCOMPARE(snapper.guides().size(), 1);
        QCOMPARE(snapper.guides().first().x1(), 100.0);
        // Held up to 8, then free.
        QCOMPARE(snapper.snap(QRectF(107, 0, 10, 10), targets, 5, 8), QPointF(-7, 0));
        QCOMPARE(snapper.snap(QRectF(109, 0, 10, 10), targets, 5, 8), QPointF(0, 0));
        QVERIFY(snapper.guides().isEmpty());
        // Six off is not caught afresh.
        QCOMPARE(snapper.snap(QRectF(106, 0, 10, 10), targets, 5, 8), QPointF(0, 0));
        // Centres meet centres.
        snapper.reset();
        QCOMPARE(snapper.snap(QRectF(0, 318, 20, 10), targets, 5, 8), QPointF(0, 2));
    }
    void equalDistancesPickTheFirstTarget() {
        Snapper snapper;
        const QVector<QRectF> targets = {QRectF(97, 500, 100, 10), QRectF(103, 600, 100, 10)};
        QCOMPARE(snapper.snap(QRectF(100, 0, 0, 0), targets, 5, 8).x(), -3.0);
    }
    void aSpotlightAlignsByItsFocusOnly() {
        SpotlightItem spot(QRectF(40, 30, 100, 60), QSizeF(800, 600));
        QCOMPARE(alignmentBounds(&spot), QRectF(40, 30, 100, 60));
    }
    void alignsAndSharesTheGaps() {
        const QVector<QRectF> boxes = {QRectF(0, 0, 10, 10), QRectF(25, 40, 20, 5), QRectF(100, 10, 30, 30)};
        QCOMPARE(alignDeltas(boxes, Align::Right), (QVector<QPointF>{{120, 0}, {85, 0}, {0, 0}}));
        QCOMPARE(alignDeltas(boxes, Align::Top), (QVector<QPointF>{{0, 0}, {0, -40}, {0, -10}}));
        // 130 wide, 60 of boxes: two gaps of 35; the outer ones stay.
        QCOMPARE(distributeDeltas(boxes, Qt::Horizontal), (QVector<QPointF>{{0, 0}, {20, 0}, {0, 0}}));
        // Overlapping boxes leave no room to share.
        QVERIFY(distributeDeltas({QRectF(0, 0, 50, 5), QRectF(10, 0, 50, 5), QRectF(20, 0, 50, 5)},
                                 Qt::Horizontal).isEmpty());
        QVERIFY(distributeDeltas({QRectF(0, 0, 5, 5), QRectF(10, 0, 5, 5)}, Qt::Horizontal).isEmpty());
    }
    void draggingSnapsUnlessControlIsHeld() {
        QGraphicsScene scene(0, 0, 400, 200);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(400, 200, QImage::Format_RGB32));
        tools.setTool(ToolType::Move);
        box(scene, QPointF(50, 50));
        RectItem *b = box(scene, QPointF(210, 120));
        RectItem *c = box(scene, QPointF(300, 120));
        Canvas canvas(&scene, &tools);
        canvas.setAnimationsEnabled(false);
        canvas.resize(440, 240);
        canvas.show();
        canvas.resetZoom();
        auto drag = [&](RectItem *item, QPointF by, Qt::KeyboardModifiers modifiers) {
            const QPoint from = canvas.mapFromScene(item->sceneBoundingRect().center());
            const QPoint to = canvas.mapFromScene(item->sceneBoundingRect().center() + by);
            QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, from);
            moveTo(canvas.viewport(), (from + to) / 2, modifiers);
            moveTo(canvas.viewport(), to, modifiers);
            const bool guided = !canvas.guides().isEmpty();
            QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, to);
            return guided;
        };
        // 3 px below the other box's top: it lands on it, with a guide while dragging.
        QVERIFY(drag(b, QPointF(0, -67), Qt::NoModifier));
        QCOMPARE(b->pos().y(), 50.0);
        QVERIFY(canvas.guides().isEmpty());   // gone on release
        undo.undo();
        QCOMPARE(b->pos().y(), 120.0);
        // Ctrl leaves it where the pointer put it.
        drag(b, QPointF(0, -67), Qt::ControlModifier);
        QCOMPARE(b->pos().y(), 53.0);
        undo.undo();
        // A group snaps as one and keeps its spacing.
        scene.clearSelection();
        b->setSelected(true);
        c->setSelected(true);
        drag(b, QPointF(0, -67), Qt::NoModifier);
        QCOMPARE(b->pos().y(), 50.0);
        QCOMPARE(c->pos().y(), 50.0);
        QCOMPARE(c->pos().x() - b->pos().x(), 90.0);
        // Switched off in the context menu's setting, nothing snaps.
        undo.undo();
        canvas.setSnapping(false);
        scene.clearSelection();
        drag(b, QPointF(0, -67), Qt::NoModifier);
        QCOMPARE(b->pos().y(), 53.0);
    }
};

QTEST_MAIN(TestSnapping)
#include "test_snapping.moc"

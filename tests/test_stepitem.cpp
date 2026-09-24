#include <QtTest>
#include <QGraphicsScene>
#include <QPainter>
#include <QUndoStack>
#include "editorwindow.h"
#include "projectcodec.h"
#include "stepbar.h"
#include "toolcontroller.h"
#include "undocommands.h"
#include "items/stepitem.h"

using namespace eddy;

// The white digits' ink box, in item coordinates.
static QRectF digitInk(const StepItem &step) {
    const int side = int(std::ceil(step.boundingRect().width())) + 4;
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.translate(side / 2.0, side / 2.0);
    const_cast<StepItem &>(step).paint(&p, nullptr, nullptr);
    p.end();
    int left = side, right = -1, top = side, bottom = -1;
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            if (qGreen(image.pixel(x, y)) > 160) {
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = std::max(bottom, y);
            }
    return QRectF(left - side / 2.0, top - side / 2.0, right - left + 1, bottom - top + 1);
}

static QList<int> numbers(QGraphicsScene &scene) {
    QList<int> out;
    for (QGraphicsItem *item : scene.items(Qt::AscendingOrder))
        if (auto *step = dynamic_cast<StepItem *>(item)) out.append(step->number());
    return out;
}

class TestStepItem : public QObject {
    Q_OBJECT
private slots:
    void digitsSitInTheMiddleAndWidenTheCircle() {
        for (int n : {1, 8, 10, 99, 100}) {
            StepItem step(n);
            step.setStrokeColor(QColor(30, 60, 200));
            const QRectF ink = digitInk(step);
            QVERIFY2(qAbs(ink.center().x()) <= 1.0 && qAbs(ink.center().y()) <= 1.0,
                     qPrintable(QStringLiteral("%1: %2,%3").arg(n).arg(ink.center().x()).arg(ink.center().y())));
            QVERIFY(ink.width() < step.diameter() - 8);   // the digits keep clear of the rim
        }
        QCOMPARE(StepItem(8).diameter(), StepItem(1).diameter());
        QVERIFY(StepItem(100).diameter() > StepItem(10).diameter());
        QVERIFY(StepItem(1, StepItem::Size::L).diameter() > StepItem(1, StepItem::Size::S).diameter());
    }
    void clicksCountOnAndUndoKeepsTheNumbers() {
        QGraphicsScene scene(0, 0, 400, 300);
        QUndoStack undo;
        ToolController tools(&scene, &undo, QImage(400, 300, QImage::Format_RGB32));
        tools.setAnimationsEnabled(false);
        tools.setTool(ToolType::Step);
        QCOMPARE(toolFromName(QStringLiteral("step")), ToolType::Step);
        for (int x : {50, 120, 190}) {
            tools.begin(QPointF(x, 50));
            tools.finish(QPointF(x, 50));
        }
        QCOMPARE(numbers(scene), (QList<int>{1, 2, 3}));
        // Removing the last and undoing brings its number back; nothing else moves.
        auto *third = dynamic_cast<StepItem *>(scene.itemAt(QPointF(190, 50), QTransform()));
        QVERIFY(third);
        undo.push(new RemoveItemCommand(&scene, third));
        QCOMPARE(numbers(scene), (QList<int>{1, 2}));
        undo.undo();
        QCOMPARE(numbers(scene), (QList<int>{1, 2, 3}));
        // A copy takes the next number.
        scene.clearSelection();
        scene.itemAt(QPointF(50, 50), QTransform())->setSelected(true);
        QVERIFY(tools.duplicateSelection(QPointF(10, 10)));
        QCOMPARE(numbers(scene), (QList<int>{1, 2, 3, 4}));
    }
    void renumberingIsOneStepAndProjectsKeepSteps() {
        Config cfg; cfg.animations = false;
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(Qt::white);
        MediaDocument doc;
        doc.image = image;
        EditorWindow w(doc, cfg, {});
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *undo = w.findChild<QUndoStack *>();
        for (int n : {5, 2, 9}) {
            auto *step = new StepItem(n, n == 2 ? StepItem::Size::L : StepItem::Size::M);
            step->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            step->setPos(40 * n, 60);
            undo->push(new AddItemCommand(scene, step));
        }
        const int steps = undo->count();
        scene->items().first()->setSelected(true);
        emit w.findChild<StepBar *>()->renumberRequested();
        QCOMPARE(numbers(*scene), (QList<int>{1, 2, 3}));
        QCOMPARE(undo->count(), steps + 1);
        undo->undo();
        QCOMPARE(numbers(*scene), (QList<int>{5, 2, 9}));
        // Deleting and restoring the first one puts it on top, yet it was made first.
        auto *first = dynamic_cast<StepItem *>(scene->itemAt(QPointF(200, 60), QTransform()));
        QVERIFY(first && first->number() == 5);
        undo->push(new RemoveItemCommand(scene, first));
        undo->undo();
        QCOMPARE(numbers(*scene), (QList<int>{2, 9, 5}));
        emit w.findChild<StepBar *>()->renumberRequested();
        QCOMPARE(first->number(), 1);
        undo->undo();
        QCOMPARE(numbers(*scene), (QList<int>{2, 9, 5}));
        // Number and size come back from a project.
        QString error;
        const auto made = itemsFromJson(itemsToJson(scene->items(Qt::AscendingOrder)), image, image.size(), &error);
        QVERIFY2(made, qPrintable(error));
        QCOMPARE(made->size(), 3);
        StepItem *big = nullptr;
        for (QGraphicsItem *item : *made)
            if (auto *step = dynamic_cast<StepItem *>(item); step && step->number() == 2) big = step;
        QVERIFY(big && big->number() == 2 && big->size() == StepItem::Size::L && big->pos() == QPointF(80, 60));
        qDeleteAll(*made);
    }
};

QTEST_MAIN(TestStepItem)
#include "test_stepitem.moc"

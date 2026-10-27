#include <QtTest>
#include <QCoreApplication>
#include <QGraphicsScene>
#include <QUndoStack>
#include "canvas/selectionhandles.h"
#include "items/shapes/rectitem.h"
#include "items/regions/redactitem.h"
#include "items/labels/textitem.h"
#include <QImage>
#include <QPainter>
#include <QGraphicsView>
#include <QApplication>
#include "app/theme.h"
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
        // scene::changed is queued, and the freshly added handles take one more pass.
        QTRY_COMPARE(handles.handleScenePos(0), before + QPointF(30, 20));
    }
    void gripsStandOutOnDarkContent() {
        // On a dark screenshot the old grip (the palette's highlight) measured 1.4:1.
        qApp->setPalette(theme::palette(true));
        QGraphicsScene scene(0, 0, 100, 100);
        auto *r = new RectItem(QRectF(20, 20, 50, 40));
        r->setFlags(QGraphicsItem::ItemIsSelectable);
        scene.addItem(r);
        SelectionHandles handles(&scene);
        r->setSelected(true);
        QImage shot(100, 100, QImage::Format_ARGB32_Premultiplied);
        shot.fill(QColor(0x20, 0x20, 0x20));
        QPainter painter(&shot);
        scene.render(&painter, QRectF(0, 0, 100, 100), QRectF(0, 0, 100, 100));
        painter.end();
        const auto luminance = [](QColor c) {
            auto ch = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
        };
        const qreal grip = luminance(shot.pixelColor(45, 20)), dark = luminance(QColor(0x20, 0x20, 0x20));
        const qreal contrast = (std::max(grip, dark) + 0.05) / (std::min(grip, dark) + 0.05);
        QVERIFY2(contrast >= 3, qPrintable(QString::number(contrast)));
    }
    void selectedTextShowsASolidFrame() {
        // Qt's own box is dashed black and white; ours runs unbroken.
        qApp->setPalette(theme::palette(true));
        QGraphicsScene scene(0, 0, 300, 120);
        auto *text = new TextItem(QStringLiteral("Frame me"), Qt::white, 20);
        text->setPos(40, 40);
        scene.addItem(text);
        text->setSelected(true);
        QGraphicsView view(&scene);
        view.setFrameShape(QFrame::NoFrame);
        view.resize(300, 120);
        view.setSceneRect(0, 0, 300, 120);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QImage shot = view.viewport()->grab().toImage();
        const QRect frame = view.mapFromScene(text->mapToScene(text->boundingRect())).boundingRect();
        int changes = 0;
        for (int x = frame.left() + 2; x < frame.right() - 2; ++x)
            changes += shot.pixelColor(x, frame.top()) != shot.pixelColor(x + 1, frame.top());
        QVERIFY2(changes == 0, qPrintable(QString::number(changes)));
    }
};
QTEST_MAIN(TestSelectionHandles)
#include "test_selectionhandles.moc"

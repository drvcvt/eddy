#include <QtTest>
#include <QGraphicsScene>
#include <QUndoStack>
#include <QWheelEvent>
#include "canvas/canvas.h"
#include "edit/toolcontroller.h"
using namespace eddy;

namespace {
// What a wheel or touchpad delivers: `angle` in eighths of a degree, 120 per notch.
void wheel(Canvas &canvas, QPoint pos, QPoint angle) {
    QWheelEvent event(QPointF(pos), QPointF(canvas.viewport()->mapToGlobal(pos)), QPoint(), angle,
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas.viewport(), &event);
}
qreal distance(QPointF a, QPointF b) { return QLineF(a, b).length(); }
// Where a scene point shows, unrounded (mapFromScene returns whole pixels).
QPointF shown(const Canvas &canvas, QPointF scene) { return canvas.viewportTransform().map(scene); }
}

// Wheel zoom: proportional to the delta, unanimated, anchored on the pointer, also mid-drag.
class TestCanvasZoom : public QObject {
    Q_OBJECT
    QGraphicsScene m_scene{0, 0, 2000, 1200};
    QUndoStack m_undo;
    ToolController m_tools{&m_scene, &m_undo, QImage(10, 10, QImage::Format_ARGB32_Premultiplied)};
private slots:
    void aNotchInSmallStepsZoomsLikeOneNotch() {
        Canvas fine(&m_scene, &m_tools), coarse(&m_scene, &m_tools);
        for (Canvas *c : {&fine, &coarse}) { c->resize(400, 300); c->show(); c->resetZoom(); }
        for (int i = 0; i < 8; ++i) wheel(fine, QPoint(200, 150), QPoint(0, 15));
        wheel(coarse, QPoint(200, 150), QPoint(0, 120));
        QVERIFY2(qAbs(fine.zoom() - coarse.zoom()) < 1e-9,
                 qPrintable(QStringLiteral("8 x 15: %1, 1 x 120: %2").arg(fine.zoom()).arg(coarse.zoom())));
    }
    void sidewaysScrollDoesNotZoom() {
        Canvas canvas(&m_scene, &m_tools);
        canvas.resize(400, 300); canvas.show(); canvas.resetZoom();
        wheel(canvas, QPoint(200, 150), QPoint(120, 0));
        QCOMPARE(canvas.zoom(), 1.0);
    }
    void wheelZoomIsImmediateEvenWithAnimations() {
        Canvas canvas(&m_scene, &m_tools);
        canvas.setAnimationsEnabled(true);
        canvas.resize(400, 300); canvas.show(); canvas.resetZoom();
        wheel(canvas, QPoint(200, 150), QPoint(0, 120));
        QCOMPARE(canvas.transform().m11(), canvas.zoom());
    }
    void pointUnderThePointerStaysThereInAndOut() {
        Canvas canvas(&m_scene, &m_tools);
        canvas.resize(400, 300); canvas.show(); canvas.resetZoom();
        canvas.centerOn(1000, 600);
        const QPoint pointer(90, 210);
        const QPointF grabbed = canvas.mapToScene(pointer);
        for (int i = 0; i < 12; ++i) wheel(canvas, pointer, QPoint(0, 120));
        QVERIFY2(distance(shown(canvas, grabbed), pointer) <= 1.0, "drifted while zooming in");
        for (int i = 0; i < 12; ++i) wheel(canvas, pointer, QPoint(0, -120));
        QVERIFY2(distance(shown(canvas, grabbed), pointer) <= 1.0, "drifted while zooming out");
    }
    void zoomingMidPanKeepsTheGrabbedPointUnderThePointer() {
        Canvas canvas(&m_scene, &m_tools);
        canvas.resize(400, 300); canvas.show(); canvas.resetZoom();
        canvas.centerOn(1000, 600);
        QPoint pointer(200, 150);
        const QPointF grabbed = canvas.mapToScene(pointer);
        QTest::mousePress(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, pointer);
        for (const QPoint step : {QPoint(-60, -40), QPoint(30, 50)}) {
            pointer += step;
            QMouseEvent move(QEvent::MouseMove, QPointF(pointer), QPointF(canvas.viewport()->mapToGlobal(pointer)),
                             Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(canvas.viewport(), &move);
            wheel(canvas, pointer, QPoint(0, 120));
            QVERIFY2(distance(shown(canvas, grabbed), pointer) <= 1.0,
                     qPrintable(QStringLiteral("grabbed point at %1,%2, pointer at %3,%4")
                         .arg(shown(canvas, grabbed).x()).arg(shown(canvas, grabbed).y())
                         .arg(pointer.x()).arg(pointer.y())));
        }
        QTest::mouseRelease(canvas.viewport(), Qt::MiddleButton, Qt::NoModifier, pointer);
    }
};

QTEST_MAIN(TestCanvasZoom)
#include "test_canvaszoom.moc"

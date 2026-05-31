#include "canvas.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>

namespace eddy {

Canvas::Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent)
    : QGraphicsView(scene, parent), m_tools(tools) {
    setRenderHint(QPainter::Antialiasing, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setCacheMode(QGraphicsView::CacheBackground);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
}

void Canvas::wheelEvent(QWheelEvent *e) {
    const double factor = e->angleDelta().y() > 0 ? 1.15 : 1/1.15;
    m_zoom *= factor;
    scale(factor, factor);
    e->accept();
}

void Canvas::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton) {
        m_dragging = true; setDragMode(QGraphicsView::ScrollHandDrag);
        QGraphicsView::mousePressEvent(e); return;
    }
    if (e->button() == Qt::LeftButton && m_tools->tool() != ToolType::Move) {
        m_tools->begin(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mousePressEvent(e); // Move tool: native selection/drag
}

void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (!m_dragging && (e->buttons() & Qt::LeftButton) && m_tools->tool() != ToolType::Move) {
        m_tools->update(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (m_dragging && e->button() == Qt::MiddleButton) {
        m_dragging = false; setDragMode(QGraphicsView::NoDrag);
        QGraphicsView::mouseReleaseEvent(e); return;
    }
    if (e->button() == Qt::LeftButton && m_tools->tool() != ToolType::Move) {
        m_tools->finish(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

}

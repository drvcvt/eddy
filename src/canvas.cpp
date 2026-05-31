#include "canvas.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QGraphicsItem>

namespace eddy {

Canvas::Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent)
    : QGraphicsView(scene, parent), m_tools(tools) {
    setRenderHint(QPainter::Antialiasing, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setCacheMode(QGraphicsView::CacheBackground);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
    // Clean look: no scrollbars; pan via middle-drag, navigate via zoom.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(QColor("#0e0f13"));
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
    if (e->button() == Qt::LeftButton && m_tools->tool() == ToolType::Text) {
        // Stamp an editable text box at the click and focus it for typing.
        QGraphicsItem *t = m_tools->placeText(mapToScene(e->pos()));
        setFocus();
        if (t) t->setFocus();
        e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && !isPointerTool()) {
        m_tools->begin(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mousePressEvent(e); // Move/Text: native selection/edit
}

void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (!m_dragging && (e->buttons() & Qt::LeftButton) && !isPointerTool()) {
        m_tools->update(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (m_dragging && e->button() == Qt::MiddleButton) {
        m_dragging = false; setDragMode(QGraphicsView::NoDrag);
        QGraphicsView::mouseReleaseEvent(e); return;
    }
    if (e->button() == Qt::LeftButton && !isPointerTool()) {
        m_tools->finish(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

}

#include "canvas.h"
#include "loupe.h"
#include <QPainter>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QGraphicsItem>
#include <QVariantAnimation>
#include <QPixmap>
#include <QCursor>

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
    setBackgroundBrush(QColor("#121212"));
}

void Canvas::startEyedropper() {
    if (m_eyedropper) return;
    const QPixmap shot = viewport()->grab();      // what the user sees: bg + annotations
    m_eyeDpr = shot.devicePixelRatio();
    m_eyeShot = shot.toImage();
    m_eyedropper = true;
    if (!m_loupe) m_loupe = new Loupe(viewport());
    viewport()->setMouseTracking(true);           // follow the cursor without a button held
    viewport()->setCursor(Qt::CrossCursor);
    updateLoupe(viewport()->mapFromGlobal(QCursor::pos()));
}

void Canvas::cancelEyedropper() {
    if (!m_eyedropper) return;
    m_eyedropper = false;
    m_eyeShot = QImage();
    if (m_loupe) m_loupe->hide();
    viewport()->unsetCursor();
}

QPoint Canvas::sourcePixel(const QPoint &viewPos) const {
    return QPoint(qRound(viewPos.x() * m_eyeDpr), qRound(viewPos.y() * m_eyeDpr));
}

void Canvas::updateLoupe(const QPoint &viewPos) {
    if (!m_eyedropper || !m_loupe) return;
    m_loupe->showAt(viewPos, m_eyeShot, sourcePixel(viewPos));
}

void Canvas::wheelEvent(QWheelEvent *e) {
    const double step = e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15;
    m_targetZoom *= step;
    if (!m_animations) { m_zoom *= step; scale(step, step); emit viewChanged(); e->accept(); return; }
    if (!m_zoomAnim) {
        m_zoomAnim = new QVariantAnimation(this);
        m_zoomAnim->setDuration(110);
        m_zoomAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_zoomAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v){
            const double target = v.toDouble();
            const double inc = target / m_zoom;       // AnchorUnderMouse keeps cursor fixed
            m_zoom = target;
            scale(inc, inc);
            emit viewChanged();
        });
    }
    m_zoomAnim->stop();
    m_zoomAnim->setStartValue(m_zoom);
    m_zoomAnim->setEndValue(m_targetZoom);
    m_zoomAnim->start();
    e->accept();
}

void Canvas::mousePressEvent(QMouseEvent *e) {
    if (m_eyedropper) {
        if (e->button() == Qt::LeftButton) {
            const QColor c = loupeSampleColor(m_eyeShot, sourcePixel(e->pos()));
            cancelEyedropper();
            emit colorPicked(c);
        } else {
            cancelEyedropper();                     // right/other button cancels
        }
        e->accept(); return;
    }
    if (e->button() == Qt::MiddleButton) {
        // Manual middle-drag pan. (ScrollHandDrag only grabs the left button, so it
        // can't pan on a middle-press — scroll the view by the cursor delta instead.)
        m_dragging = true;
        m_panLast = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept(); return;
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
    if (m_eyedropper) { updateLoupe(e->pos()); e->accept(); return; }
    if (m_dragging) {                                   // middle-drag pan
        const QPoint d = e->pos() - m_panLast;
        m_panLast = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        emit viewChanged();                             // overlays (mode-bar) re-anchor
        e->accept(); return;
    }
    if ((e->buttons() & Qt::LeftButton) && !isPointerTool()) {
        m_tools->update(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (m_dragging && e->button() == Qt::MiddleButton) {
        m_dragging = false;
        viewport()->unsetCursor();
        e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && !isPointerTool()) {
        m_tools->finish(mapToScene(e->pos())); e->accept(); return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void Canvas::resizeEvent(QResizeEvent *e) {
    QGraphicsView::resizeEvent(e);
    emit viewChanged();
}

}

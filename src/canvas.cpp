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
#include <QKeyEvent>
#include <QCoreApplication>
#include <QApplication>
#include <QtMath>
#include "items/textitem.h"

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
    setBackgroundBrush(QApplication::palette().color(QPalette::Window));
    connect(m_tools, &ToolController::toolChanged, this, [this]{ updateCursor(); });
    updateCursor();
}

void Canvas::updateCursor() {
    if (m_eyedropper) {
        viewport()->setCursor(Qt::CrossCursor);
    } else if (m_dragging) {
        viewport()->setCursor(Qt::ClosedHandCursor);
    } else if (m_spacePan) {
        viewport()->setCursor(Qt::OpenHandCursor);
    } else if (m_tools->tool() == ToolType::Text) {
        viewport()->setCursor(Qt::IBeamCursor);
    } else if (m_tools->tool() != ToolType::Move) {
        viewport()->setCursor(Qt::CrossCursor);
    } else {
        viewport()->unsetCursor();
    }
}

void Canvas::setSpacePan(bool on) {
    m_spacePan = on;
    updateCursor();
}

void Canvas::zoomBy(double factor) {
    const double next = qBound(0.05, m_targetZoom * factor, 32.0);
    if (qFuzzyCompare(next, m_targetZoom)) return;
    m_targetZoom = next;
    if (!m_animations) {
        const double inc = m_targetZoom / m_zoom;
        m_zoom = m_targetZoom;
        scale(inc, inc);
        emit viewChanged();
        return;
    }
    if (!m_zoomAnim) {
        m_zoomAnim = new QVariantAnimation(this);
        m_zoomAnim->setDuration(110);
        m_zoomAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_zoomAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v){
            const double target = v.toDouble();
            const double inc = target / m_zoom;
            m_zoom = target;
            scale(inc, inc);
            emit viewChanged();
        });
    }
    m_zoomAnim->stop();
    m_zoomAnim->setStartValue(m_zoom);
    m_zoomAnim->setEndValue(m_targetZoom);
    m_zoomAnim->start();
}

void Canvas::resetZoom() {
    if (m_zoomAnim) m_zoomAnim->stop();
    resetTransform();
    m_zoom = m_targetZoom = 1.0;
    emit viewChanged();
}

void Canvas::fitMedia() {
    if (m_zoomAnim) m_zoomAnim->stop();
    resetTransform();
    fitInView(sceneRect(), Qt::KeepAspectRatio);
    m_zoom = m_targetZoom = transform().m11();
    emit viewChanged();
}

void Canvas::startEyedropper() {
    if (m_eyedropper) return;
    const QPixmap shot = viewport()->grab();      // what the user sees: bg + annotations
    m_eyeDpr = shot.devicePixelRatio();
    m_eyeShot = shot.toImage();
    m_eyedropper = true;
    if (!m_loupe) m_loupe = new Loupe(viewport());
    m_eyeTrackPrev = viewport()->hasMouseTracking();
    viewport()->setMouseTracking(true);           // follow the cursor without a button held
    updateCursor();
    updateLoupe(viewport()->mapFromGlobal(QCursor::pos()));
}

void Canvas::cancelEyedropper() {
    if (!m_eyedropper) return;
    m_eyedropper = false;
    m_eyeShot = QImage();
    if (m_loupe) m_loupe->hide();
    updateCursor();
    viewport()->setMouseTracking(m_eyeTrackPrev);
}

QPoint Canvas::sourcePixel(const QPoint &viewPos) const {
    return QPoint(qRound(viewPos.x() * m_eyeDpr), qRound(viewPos.y() * m_eyeDpr));
}

void Canvas::updateLoupe(const QPoint &viewPos) {
    if (!m_eyedropper || !m_loupe) return;
    m_loupe->showAt(viewPos, m_eyeShot, sourcePixel(viewPos));
}

void Canvas::keyPressEvent(QKeyEvent *e) {
    if (scene()->focusItem() && scene()->focusItem()->type() == TextItem::Type
        && (e->key() == Qt::Key_Escape
            || ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
                && e->modifiers().testFlag(Qt::ControlModifier)))) {
        QCoreApplication::sendEvent(window(), e);
        e->accept();
        return;
    }
    if (!scene()->focusItem() && window() != this) {
        QCoreApplication::sendEvent(window(), e);
        e->accept();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        if (auto *text = dynamic_cast<TextItem *>(itemAt(e->pos()))) {
            m_tools->editText(text);
            e->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(e);
}

void Canvas::keyReleaseEvent(QKeyEvent *e) {
    if (!scene()->focusItem() && window() != this) {
        QCoreApplication::sendEvent(window(), e);
        e->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(e);
}

void Canvas::wheelEvent(QWheelEvent *e) {
    if (m_eyedropper) { e->accept(); return; }   // don't zoom under a frozen snapshot
    const double step = e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15;
    zoomBy(step);
    e->accept();
}

void Canvas::mousePressEvent(QMouseEvent *e) {
    if (m_eyedropper) {
        m_swallowRelease = e->button();
        if (e->button() == Qt::LeftButton) {
            const QColor c = loupeSampleColor(m_eyeShot, sourcePixel(e->pos()));
            cancelEyedropper();
            emit colorPicked(c);
        } else {
            cancelEyedropper();                     // right/other button cancels
        }
        e->accept(); return;
    }
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && m_spacePan)) {
        // Manual middle-drag pan. (ScrollHandDrag only grabs the left button, so it
        // can't pan on a middle-press — scroll the view by the cursor delta instead.)
        m_dragging = true;
        m_panButton = e->button();
        m_panLast = e->pos();
        updateCursor();
        e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && m_tools->tool() == ToolType::Text) {
        if (auto *text = dynamic_cast<TextItem *>(itemAt(e->pos()))) {
            QGraphicsView::mousePressEvent(e);
            if (!(text->textInteractionFlags() & Qt::TextEditorInteraction))
                m_tools->beginMove();
            return;
        }
        // Stamp an editable text box at the click and focus it for typing.
        QGraphicsItem *t = m_tools->placeText(mapToScene(e->pos()));
        setFocus();
        if (t) t->setFocus();
        e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && !isPointerTool()) {
        m_tools->begin(mapToScene(e->pos())); e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && m_tools->tool() == ToolType::Move
        && e->modifiers().testFlag(Qt::ShiftModifier)) {
        if (QGraphicsItem *item = itemAt(e->pos()); item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable))
            item->setSelected(!item->isSelected());
        m_swallowRelease = e->button();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && m_tools->tool() == ToolType::Move
        && e->modifiers().testFlag(Qt::AltModifier)) {
        QGraphicsItem *item = itemAt(e->pos());
        if (item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) {
            if (!item->isSelected()) {
                scene()->clearSelection();
                item->setSelected(true);
            }
            m_duplicateDragging = m_tools->beginDuplicateMove();
        }
    }
    QGraphicsView::mousePressEvent(e); // Move/Text: native selection/edit
    if (e->button() == Qt::LeftButton && m_tools->tool() == ToolType::Move && !m_duplicateDragging)
        m_tools->beginMove();
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
        m_tools->update(mapToScene(e->pos()), e->modifiers()); e->accept(); return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == m_swallowRelease) {
        m_swallowRelease = Qt::NoButton;
        e->accept();
        return;
    }
    if (m_dragging && e->button() == m_panButton) {
        m_dragging = false;
        m_panButton = Qt::NoButton;
        updateCursor();
        e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && !isPointerTool()) {
        m_tools->finish(mapToScene(e->pos()), e->modifiers()); e->accept(); return;
    }
    QGraphicsView::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && isPointerTool()) {
        m_tools->finishMove();
        m_duplicateDragging = false;
    }
}

void Canvas::resizeEvent(QResizeEvent *e) {
    if (m_eyedropper) cancelEyedropper();        // snapshot + loupe placement go stale on resize
    QGraphicsView::resizeEvent(e);
    emit viewChanged();
}

}

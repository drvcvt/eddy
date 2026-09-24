#include "canvas.h"
#include "loupe.h"
#include "cropcontroller.h"
#include <QPainter>
#include <QPainterPath>
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

void Canvas::cancelPan() {
    m_spacePan = m_dragging = false;
    m_panButton = Qt::NoButton;
    updateCursor();
}

void Canvas::updateNavigationBounds() {
    // The document keeps its native bounds; only the camera needs room to pan
    // when a fitted or small image has no natural scrollbar range.
    const QPointF center = mapToScene(viewport()->rect().center());
    const qreal scale = qMax(0.001, transform().m11());
    const qreal dx = viewport()->width() / scale;
    const qreal dy = viewport()->height() / scale;
    setSceneRect(scene()->sceneRect().adjusted(-dx, -dy, dx, dy)
                     .united(mapToScene(viewport()->rect()).boundingRect()));
    centerOn(center);
}

QPointF Canvas::viewportCentreInScene() const {
    return viewportTransform().inverted().map(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
}

// Every zoom, pan and fit works on the user's own view as if there were no
// camera; the camera goes back on afterwards.
template <typename Change> void Canvas::withoutCamera(Change change) {
    if (m_camera.isIdentity()) { change(); return; }
    const QTransform camera = m_camera;
    m_camera = QTransform();
    setTransform(QTransform::fromScale(m_viewZoom, m_viewZoom));
    updateNavigationBounds();
    centerOn(m_viewCentre);
    change();
    m_viewZoom = transform().m11();
    m_viewCentre = viewportCentreInScene();
    m_camera = camera;
    applyCamera();
}

void Canvas::applyCamera() {
    const qreal k = m_camera.m11();
    setTransform(QTransform::fromScale(m_viewZoom * k, m_viewZoom * k));
    updateNavigationBounds();
    centerOn(m_camera.inverted().map(m_viewCentre));
}

QTransform Canvas::viewWithoutCamera() const {
    if (m_camera.isIdentity()) return viewportTransform();
    return QTransform::fromTranslate(-m_viewCentre.x(), -m_viewCentre.y())
         * QTransform::fromScale(m_viewZoom, m_viewZoom)
         * QTransform::fromTranslate(viewport()->width() / 2.0, viewport()->height() / 2.0);
}

void Canvas::setCamera(const QRectF &camera) {
    const QRectF base = contentRect();
    QTransform next;
    if (!camera.isEmpty() && !base.isEmpty() && camera != base) {
        const qreal k = base.width() / camera.width();
        next = QTransform::fromTranslate(-camera.left(), -camera.top()) * QTransform::fromScale(k, k)
             * QTransform::fromTranslate(base.left(), base.top());
    }
    m_cameraRect = next.isIdentity() ? QRectF() : camera;
    if (next == m_camera) return;
    if (m_camera.isIdentity()) {
        m_viewZoom = transform().m11();
        m_viewCentre = viewportCentreInScene();
    }
    m_camera = next;
    applyCamera();
    viewport()->update();
    emit viewChanged();
}

void Canvas::cancelCameraDrag() {
    if (!m_cameraDragging) return;
    m_cameraDragging = false;
    updateCursor();
    emit cameraDragFinished(true);
}

void Canvas::zoomBy(double factor) {
    const double next = qBound(0.05, m_targetZoom * factor, 32.0);
    if (qFuzzyCompare(next, m_targetZoom)) return;
    m_fitted = false;
    m_targetZoom = next;
    if (!m_animations) {
        const double inc = m_targetZoom / m_zoom;
        m_zoom = m_targetZoom;
        withoutCamera([&] { scale(inc, inc); updateNavigationBounds(); });
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
            withoutCamera([&] { scale(inc, inc); updateNavigationBounds(); });
            emit viewChanged();
        });
    }
    m_zoomAnim->stop();
    m_zoomAnim->setStartValue(m_zoom);
    m_zoomAnim->setEndValue(m_targetZoom);
    m_zoomAnim->start();
}

void Canvas::resetZoom() {
    m_fitted = false;
    if (m_zoomAnim) m_zoomAnim->stop();
    withoutCamera([&] {
        resetTransform();
        m_zoom = m_targetZoom = 1.0;
        updateNavigationBounds();
    });
    emit viewChanged();
}

void Canvas::fitMedia() {
    if (m_zoomAnim) m_zoomAnim->stop();
    withoutCamera([&] {
        resetTransform();
        m_fitted = true;
        QRectF fittedRect = (m_crop && m_crop->active()) ? contentRect() : viewRect();
        if (m_crop && m_crop->active()) {
            const qreal scale = qMax(0.001, qMin((viewport()->width() - 4.0) / fittedRect.width(),
                                                (viewport()->height() - 4.0) / fittedRect.height()));
            fittedRect.adjust(-10 / scale, -10 / scale, 10 / scale, 10 / scale);
        }
        fitInView(fittedRect, Qt::KeepAspectRatio);
        m_zoom = m_targetZoom = transform().m11();
        updateNavigationBounds();
        centerOn(fittedRect.center());
    });
    emit viewChanged();
}

QRectF Canvas::viewRect() const {
    return m_studioOutput.isEmpty() ? contentRect() : m_studioOutput;
}

void Canvas::setStudioFrame(const QPixmap &background, QRectF output, qreal radius) {
    m_studioBackground = background;
    m_studioOutput = output;
    m_studioRadius = radius;
    if (m_fitted) fitMedia();
    viewport()->update();
}

void Canvas::clearStudioFrame() {
    if (m_studioOutput.isEmpty()) return;
    m_studioBackground = {};
    m_studioOutput = {};
    if (m_fitted) fitMedia();
    viewport()->update();
}

QRectF Canvas::contentRect() const {
    return m_contentRect.isEmpty() ? scene()->sceneRect() : m_contentRect;
}

void Canvas::setContentRect(QRectF rect) {
    m_contentRect = rect;
    if (m_fitted) fitMedia();
    if (!m_cameraRect.isEmpty()) {
        // The camera maps onto the content rect, which just moved.
        const QRectF camera = m_cameraRect;
        setCamera({});
        setCamera(camera);
    }
    viewport()->update();
}

void Canvas::restoreView(const QTransform &transform, QPointF center, bool fitted) {
    if (m_zoomAnim) m_zoomAnim->stop();
    withoutCamera([&] {
        setTransform(transform);
        m_zoom = m_targetZoom = transform.m11();
        m_fitted = fitted;
        updateNavigationBounds();
        centerOn(center);
    });
    emit viewChanged();
}

void Canvas::setCropController(CropController *crop) {
    m_crop = crop;
    viewport()->setMouseTracking(true);
    connect(crop, &CropController::changed, this, [this] {
        viewport()->update();
        updateCursor();
    });
}

void Canvas::drawForeground(QPainter *painter, const QRectF &) {
    const bool cropping = m_crop && m_crop->active();
    if (!cropping && !m_studioOutput.isEmpty()) {
        // Same picture as the export: the background around rounded content,
        // covering anything drawn past the content's edge or corners.
        painter->save();
        painter->resetTransform();
        painter->setRenderHint(QPainter::Antialiasing);
        const QTransform view = viewWithoutCamera();
        const QRectF output = view.mapRect(m_studioOutput);
        const QRectF content = view.mapRect(contentRect());
        const qreal radius = m_studioRadius * view.m11();
        QPainterPath outside;
        outside.addRect(viewport()->rect());
        outside.addRect(output);
        painter->fillPath(outside, palette().color(QPalette::Window));
        QPainterPath ring;
        ring.addRect(output);
        ring.addRoundedRect(content, radius, radius);
        QBrush brush(m_studioBackground);
        brush.setTransform(QTransform::fromScale(output.width() / m_studioBackground.width(),
                                                 output.height() / m_studioBackground.height())
                           * QTransform::fromTranslate(output.x(), output.y()));
        painter->fillPath(ring, brush);
        painter->restore();
        return;
    }
    // Zoomed or cropped, the picture ends at the content's edge, as in the export.
    if (!cropping && contentRect() == scene()->sceneRect() && m_camera.isIdentity()) return;
    painter->save();
    painter->resetTransform();
    const QRectF frame = cropping ? viewportTransform().mapRect(m_crop->rect())
                                  : viewWithoutCamera().mapRect(contentRect());
    QPainterPath outside;
    outside.addRect(viewport()->rect());
    outside.addRect(frame);
    painter->fillPath(outside, cropping ? QColor(0, 0, 0, 155) : palette().color(QPalette::Window));
    if (cropping) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(QColor(255, 255, 255, 70), 1));
        for (int i = 1; i < 3; ++i) {
            const qreal x = frame.left() + frame.width() * i / 3;
            const qreal y = frame.top() + frame.height() * i / 3;
            painter->drawLine(QPointF(x, frame.top()), QPointF(x, frame.bottom()));
            painter->drawLine(QPointF(frame.left(), y), QPointF(frame.right(), y));
        }
        painter->setPen(QPen(QColor(255, 255, 255, 210), 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(frame);
        painter->setPen(QPen(palette().color(QPalette::Window), 1));
        painter->setBrush(palette().color(QPalette::WindowText));
        for (const auto &point : m_crop->handles()) {
            const QPointF p = viewportTransform().map(point);
            painter->drawRoundedRect(QRectF(p - QPointF(5, 5), QSizeF(10, 10)), 3, 3);
        }
    }
    painter->restore();
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
    if ((m_crop && m_crop->active()) || !contentRect().contains(mapToScene(e->pos()))) {
        e->accept(); return;
    }
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
        m_fitted = false;
        m_panButton = e->button();
        m_panLast = e->pos();
        updateCursor();
        e->accept(); return;
    }
    if (m_crop && m_crop->active()) {
        if (e->button() == Qt::LeftButton)
            m_crop->press(mapToScene(e->pos()), 10 / transform().m11(), e->modifiers());
        e->accept(); return;
    }
    if (e->button() == Qt::LeftButton && !contentRect().contains(mapToScene(e->pos()))) {
        m_swallowRelease = e->button();
        scene()->clearSelection();
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
    if (e->button() == Qt::LeftButton && m_cameraDrag && m_tools->tool() == ToolType::Move
        && e->modifiers() == Qt::NoModifier) {
        const QGraphicsItem *item = itemAt(e->pos());
        if (!item || item->zValue() <= -1000) {
            scene()->clearSelection();
            m_cameraDragging = true;
            m_cameraDragLast = e->position();
            viewport()->setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }
    }
    QGraphicsView::mousePressEvent(e); // Move/Text: native selection/edit
    if (e->button() == Qt::LeftButton && m_tools->tool() == ToolType::Move && !m_duplicateDragging)
        m_tools->beginMove();
}

void Canvas::mouseMoveEvent(QMouseEvent *e) {
    if (m_eyedropper) { updateLoupe(e->pos()); e->accept(); return; }
    if (m_cameraDragging) {
        const QPointF delta = (e->position() - m_cameraDragLast) / transform().m11();
        m_cameraDragLast = e->position();
        emit cameraDragged(delta);
        e->accept();
        return;
    }
    if (m_dragging) {                                   // middle-drag pan
        const QPoint d = e->pos() - m_panLast;
        m_panLast = e->pos();
        withoutCamera([&] {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        });
        emit viewChanged();                             // overlays (mode-bar) re-anchor
        e->accept(); return;
    }
    if (m_crop && m_crop->active()) {
        if (e->buttons() & Qt::LeftButton) m_crop->move(mapToScene(e->pos()), e->modifiers());
        if (!m_spacePan) viewport()->setCursor(m_crop->cursor(mapToScene(e->pos()), 10 / transform().m11()));
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
    if (m_cameraDragging && e->button() == Qt::LeftButton) {
        m_cameraDragging = false;
        updateCursor();
        emit cameraDragFinished(false);
        e->accept();
        return;
    }
    if (m_dragging && e->button() == m_panButton) {
        m_dragging = false;
        m_panButton = Qt::NoButton;
        updateCursor();
        e->accept(); return;
    }
    if (m_crop && m_crop->active()) {
        if (e->button() == Qt::LeftButton) {
            m_crop->move(mapToScene(e->pos()), e->modifiers());
            m_crop->release();
        }
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
    withoutCamera([&] { if (m_fitted) fitMedia(); else updateNavigationBounds(); });
    emit viewChanged();
}

}

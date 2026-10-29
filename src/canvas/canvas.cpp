#include "canvas/canvas.h"
#include "app/theme.h"
#include <QContextMenuEvent>
#include <QMenu>
#include <utility>
#include "chrome/loupe.h"
#include "edit/cropcontroller.h"
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QGraphicsItem>
#include "app/spring.h"
#include <QPixmap>
#include <QCursor>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QApplication>
#include <QtMath>
#include <algorithm>
#include "items/labels/textitem.h"

namespace eddy {

Canvas::Canvas(QGraphicsScene *scene, ToolController *tools, QWidget *parent)
    : QGraphicsView(scene, parent), m_tools(tools) {
    setRenderHint(QPainter::Antialiasing, true);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    // Zoom anchors itself (zoomAt): Qt's AnchorUnderMouse uses the last pointer move the
    // base view saw, which pans, drags and drawing never pass on.
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::NoFrame);
    // Clean look: no scrollbars; pan via middle-drag, navigate via zoom.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(workspaceColor());
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
    const QPointF center = viewportCentreInScene();   // exact: rect().center() sits half a pixel off
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
    const qreal scale = m_viewZoom * m_camera.m11();
    const QPointF centre = m_camera.inverted().map(m_viewCentre);
    setTransform(QTransform::fromScale(scale, scale));
    updateNavigationBounds();
    centerOn(centre);
    // The scrollbars place the view on whole pixels; a moving camera would wobble by up to
    // one. The rest goes into the transform, so the camera lands exactly. Without one the
    // view stays on whole pixels, as the user left it.
    if (m_camera.isIdentity()) return;
    const QPointF middle(viewport()->width() / 2.0, viewport()->height() / 2.0);
    const QPointF rest = middle - viewportTransform().map(centre);
    if (qAbs(rest.x()) > 1e-6 || qAbs(rest.y()) > 1e-6)
        setTransform(QTransform::fromScale(scale, scale) * QTransform::fromTranslate(rest.x(), rest.y()));
}

QTransform Canvas::viewWithoutCamera() const {
    if (m_camera.isIdentity()) return viewportTransform();
    return QTransform::fromTranslate(-m_viewCentre.x(), -m_viewCentre.y())
         * QTransform::fromScale(m_viewZoom, m_viewZoom)
         * QTransform::fromTranslate(viewport()->width() / 2.0, viewport()->height() / 2.0);
}

void Canvas::setCamera(const QRectF &wanted) {
    const QRectF base = contentRect();
    QRectF camera = wanted;
    if (!camera.isEmpty() && !base.isEmpty()) {
        // The springs never land exactly. A move under 1/16 of a device pixel cannot be
        // seen, so a camera at rest repaints nothing, and one that close to the base view
        // is the base view (no camera at all).
        const qreal device = (m_camera.isIdentity() ? transform().m11() : m_viewZoom) * devicePixelRatioF()
                           * base.width() / camera.width();
        const auto apart = [&](const QRectF &a, const QRectF &b) {
            return device * std::max({qAbs(a.left() - b.left()), qAbs(a.top() - b.top()),
                                      qAbs(a.right() - b.right()), qAbs(a.bottom() - b.bottom())});
        };
        constexpr qreal kUnseen = 1.0 / 16;
        if (apart(camera, base) < kUnseen) camera = base;
        else if (!m_cameraRect.isEmpty() && apart(camera, m_cameraRect) < kUnseen) return;
    }
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
    cacheForCamera(!m_camera.isIdentity());
    applyCamera();
    viewport()->update();
    emit viewChanged();
}

// A device cache is drawn anew whenever the view scales, so under a moving camera a long
// stroke and an arrow cost a full redraw each tick (31 ms against 2 ms, docs/probes/
// camera_paint.cpp). Under a camera they keep an item cache instead, at up to 4x (the
// deepest zoom) and at most 2048 px, which the camera scales smoothly; without one, the
// exact device cache again. Items drawn under a camera join on its next move.
void Canvas::cacheForCamera(bool camera) {
    constexpr int key = AnnotationItem::kCameraCacheKey;
    for (QGraphicsItem *item : scene()->items()) {
        if (camera && item->cacheMode() == QGraphicsItem::DeviceCoordinateCache) {
            const QSizeF size = item->boundingRect().size();
            const qreal scale = std::clamp(2048.0 / std::max({size.width(), size.height(), 1.0}), 1.0, 4.0);
            const QSize cache = (size * scale).toSize();
            item->setCacheMode(QGraphicsItem::ItemCoordinateCache, cache);
            item->setData(key, cache);
        } else if (!camera && item->data(key).isValid()) {
            item->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
            item->setData(key, QVariant());
        }
    }
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
    const QPointF centre(viewport()->width() / 2.0, viewport()->height() / 2.0);
    // A run of key presses keeps zooming about the scene point it started on.
    if (!m_zoomSpring || m_zoomSpring->state() != QAbstractAnimation::Running)
        m_zoomHeld = m_camera.isIdentity() ? sceneAt(centre) : m_viewCentre;
    m_targetZoom = next;
    if (!m_animations) { zoomAt(next, centre, m_zoomHeld); return; }
    if (!m_zoomSpring) {
        m_zoomSpring = new Spring(motion::kBase, [this](qreal zoom) {
            const double target = m_targetZoom;
            zoomAt(zoom, QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0), m_zoomHeld);
            m_targetZoom = target;
        }, this);
    }
    if (m_zoomSpring->state() != QAbstractAnimation::Running) m_zoomSpring->jumpTo(m_zoom);
    m_zoomSpring->setTarget(next);
}

QPointF Canvas::sceneAt(QPointF viewPos) const {
    return viewportTransform().inverted().map(viewPos);
}

void Canvas::zoomAt(double zoom, QPointF viewPos, QPointF scenePoint) {
    m_fitted = false;
    withoutCamera([&] {
        scale(zoom / m_zoom, zoom / m_zoom);
        m_zoom = m_targetZoom = zoom;
        updateNavigationBounds();
        // Whole scroll-bar pixels: round the remaining offset once, from the exact point.
        const QPointF off = viewportTransform().map(scenePoint) - viewPos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(off.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(off.y()));
    });
    emit viewChanged();
}

void Canvas::resetZoom() {
    m_fitted = false;
    if (m_zoomSpring) m_zoomSpring->stop();
    withoutCamera([&] {
        resetTransform();
        m_zoom = m_targetZoom = 1.0;
        updateNavigationBounds();
    });
    emit viewChanged();
}

void Canvas::fitMedia() {
    if (m_zoomSpring) m_zoomSpring->stop();
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

// Where the workspace meets the frame (the tool rail on the left, a bar above and
// below) it turns on squircle corners; on the right it runs into the window edge,
// so those stay square. Drawn by the view only, so no export carries it.
void Canvas::drawWorkspaceCorners(QPainter *painter) {
    const QRectF view(viewport()->rect());
    QPainterPath outside;
    outside.addRect(view);
    outside = outside.subtracted(theme::squircleRect(view, theme::kS6, theme::TopLeft | theme::BottomLeft));
    painter->save();
    painter->resetTransform();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->fillPath(outside, theme::tone(theme::Tone::Bg,
                                           QApplication::palette().color(QPalette::Window).lightness() < 128));
    painter->restore();
}

// A scroll moves the painted pixels along, corners included, but the corners stay on
// the viewport: paint them again where they are and where the move put their copy.
void Canvas::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    const int h = viewport()->height();
    const QRegion corners = QRegion(0, 0, theme::kS6, theme::kS6)
                          + QRegion(0, h - theme::kS6, theme::kS6, theme::kS6);
    viewport()->update(corners + corners.translated(dx, dy));
}

QColor Canvas::workspaceColor() const {
    return theme::tone(theme::Tone::Well, QApplication::palette().color(QPalette::Window).lightness() < 128);
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
    if (m_zoomSpring) m_zoomSpring->stop();
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
    drawFrame(painter);
    drawWorkspaceCorners(painter);
    if (m_guides.isEmpty()) return;
    // Thin and neutral, over everything; the view draws them, so no export has them.
    QColor ink = palette().color(QPalette::WindowText);
    ink.setAlphaF(0.6);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(ink, 0));
    for (const QLineF &line : std::as_const(m_guides)) painter->drawLine(line);
    painter->restore();
}

void Canvas::drawFrame(QPainter *painter) {
    const bool cropping = m_crop && m_crop->active();
    if (!cropping && !m_studioOutput.isEmpty()) {
        // Same picture as the export: the background around rounded content,
        // covering anything drawn past the content's edge or corners.
        painter->save();
        painter->resetTransform();
        painter->setRenderHint(QPainter::Antialiasing);
        const QTransform view = viewWithoutCamera();
        // On whole device pixels: two antialiased fills meeting on a fractional
        // edge let the video show through as a thin coloured line.
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const QRectF exact = view.mapRect(m_studioOutput);
        const QRectF output(QPointF(qRound(exact.left() * dpr), qRound(exact.top() * dpr)) / dpr,
                            QPointF(qRound(exact.right() * dpr), qRound(exact.bottom() * dpr)) / dpr);
        const QRectF content = view.mapRect(contentRect());
        const qreal radius = m_studioRadius * view.m11();
        QPainterPath outside;
        outside.addRect(viewport()->rect());
        outside.addRect(output);
        painter->fillPath(outside, workspaceColor());
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
    painter->fillPath(outside, cropping ? QColor(0, 0, 0, 155) : workspaceColor());
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
        for (const auto &point : m_crop->handles()) theme::paintGrip(painter, viewportTransform().map(point));
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
    e->accept();
    if (m_eyedropper) return;   // don't zoom under a frozen snapshot
    // 120 is one notch; high-resolution wheels and touchpads send fractions of
    // one, sideways scrolling sends none. Unanimated: each step follows the hand.
    const int delta = e->angleDelta().y();
    if (delta == 0) return;
    const double next = qBound(0.05, m_targetZoom * std::pow(1.15, delta / 120.0), 32.0);
    if (qFuzzyCompare(next, m_targetZoom)) return;
    if (m_zoomSpring) m_zoomSpring->stop();
    // With a camera on, the user's own view is not what shows: hold its centre.
    const QPointF centre(viewport()->width() / 2.0, viewport()->height() / 2.0);
    const QPointF pos = m_camera.isIdentity() ? e->position() : centre;
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    if (pos != m_wheelAnchor.view || scroll != m_wheelAnchor.scroll || m_zoom != m_wheelAnchor.zoom)
        m_wheelAnchor = {pos, m_camera.isIdentity() ? sceneAt(pos) : m_viewCentre, {}, 0};
    zoomAt(next, pos, m_wheelAnchor.scene);
    m_wheelAnchor.scroll = {horizontalScrollBar()->value(), verticalScrollBar()->value()};
    m_wheelAnchor.zoom = m_zoom;
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
            m_skipContextMenu = e->button() == Qt::RightButton;   // and that right click is spent
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
    if (e->buttons() & Qt::LeftButton) snapDrag(e->modifiers());
}

// The native move has just placed the dragged items from where the press
// started, so the snap offset never adds up over events.
void Canvas::snapDrag(Qt::KeyboardModifiers modifiers) {
    QGraphicsItem *grabbed = scene()->mouseGrabberItem();
    auto *text = dynamic_cast<TextItem *>(grabbed);
    const QList<QGraphicsItem *> &moving = m_tools->movingItems();
    if (!m_snapping || modifiers.testFlag(Qt::ControlModifier) || !isPointerTool() || !grabbed
        || !moving.contains(grabbed) || (text && text->textInteractionFlags().testFlag(Qt::TextEditorInteraction))) {
        clearGuides();
        return;
    }
    QRectF box;
    for (const QGraphicsItem *item : moving) box |= alignmentBounds(item);
    QVector<QRectF> targets = {contentRect()};
    for (const QGraphicsItem *item : scene()->items(Qt::AscendingOrder)) {
        if (!item->isVisible() || moving.contains(item) || item->parentItem()) continue;
        if (!dynamic_cast<const AnnotationItem *>(item) && !dynamic_cast<const TextItem *>(item)) continue;
        targets.append(alignmentBounds(item));
    }
    const qreal pixel = 1.0 / std::max(1e-6, transform().m11());   // logical screen px, whatever the zoom
    const QPointF offset = m_snapper.snap(box, targets, 5 * pixel, 8 * pixel);
    if (!offset.isNull())
        for (QGraphicsItem *item : moving) item->moveBy(offset.x(), offset.y());
    m_guides = m_snapper.guides();
    viewport()->update();
}

void Canvas::clearGuides() {
    m_snapper.reset();
    if (m_guides.isEmpty()) return;
    m_guides.clear();
    viewport()->update();
}

// The snap setting, where a right click means nothing else: with the Move or
// Text tool, off any text being edited, outside crop and the eyedropper.
void Canvas::contextMenuEvent(QContextMenuEvent *e) {
    if (std::exchange(m_skipContextMenu, false)) { e->accept(); return; }
    auto *text = dynamic_cast<TextItem *>(itemAt(e->pos()));
    if (!isPointerTool() || m_eyedropper || (m_crop && m_crop->active())
        || (text && text->textInteractionFlags().testFlag(Qt::TextEditorInteraction))) {
        QGraphicsView::contextMenuEvent(e);
        return;
    }
    QMenu menu(this);
    theme::roundMenu(&menu);
    QAction *snap = menu.addAction(tr("Snap to objects"));
    snap->setCheckable(true);
    snap->setChecked(m_snapping);
    connect(snap, &QAction::toggled, this, &Canvas::setSnapping);
    menu.exec(e->globalPos());
    e->accept();
}

void Canvas::focusOutEvent(QFocusEvent *e) {
    clearGuides();
    QGraphicsView::focusOutEvent(e);
}

void Canvas::mouseReleaseEvent(QMouseEvent *e) {
    clearGuides();   // whichever way the release goes
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
    // The picture keeps its place: the scene point in the middle stays in the middle.
    // A run of resize steps holds the first exact point, so whole scroll-bar pixels never add up.
    const QPoint scroll(horizontalScrollBar()->value(), verticalScrollBar()->value());
    if (scroll != m_resizeAnchor.scroll || m_zoom != m_resizeAnchor.zoom || !m_camera.isIdentity())
        m_resizeAnchor = {{}, viewportTransform().inverted().map(
            QPointF(e->oldSize().width() / 2.0, e->oldSize().height() / 2.0)), {}, 0};
    QGraphicsView::resizeEvent(e);
    withoutCamera([&] {
        if (m_fitted) { fitMedia(); return; }
        updateNavigationBounds();
        if (!m_camera.isIdentity() || !e->oldSize().isValid()) return;
        const QPointF off = viewportTransform().map(m_resizeAnchor.scene)
                            - QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0);
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(off.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(off.y()));
        m_resizeAnchor.scroll = {horizontalScrollBar()->value(), verticalScrollBar()->value()};
        m_resizeAnchor.zoom = m_zoom;
    });
    emit viewChanged();
}

}

#include "minimap.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QWheelEvent>

namespace eddy {

MiniMap::MiniMap(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("MiniMap"));
    setAttribute(Qt::WA_StyledBackground);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("Drag to move the zoom · wheel to change it"));
}

void MiniMap::setContent(const QImage &image, const QRectF &content) {
    m_image = image;
    m_content = content;
    fitSize();
    update();
}

void MiniMap::setWidthLimit(int width) {
    if (width == m_widthLimit) return;
    m_widthLimit = width;
    fitSize();
}

void MiniMap::fitSize() {
    if (m_content.isEmpty()) return;
    const double scale = qMin(m_widthLimit / m_content.width(), m_widthLimit * 112.0 / 152 / m_content.height());
    setFixedSize(qRound(m_content.width() * scale) + 8, qRound(m_content.height() * scale) + 8);
}

void MiniMap::setCamera(const QRectF &camera) {
    m_camera = camera;
    update();
}

QRectF MiniMap::windowRect() const {
    if (m_content.isEmpty() || m_camera.isEmpty()) return {};
    const QRectF inner = imageRect();
    const double sx = inner.width() / m_content.width(), sy = inner.height() / m_content.height();
    return QRectF(inner.left() + (m_camera.left() - m_content.left()) * sx,
                  inner.top() + (m_camera.top() - m_content.top()) * sy,
                  m_camera.width() * sx, m_camera.height() * sy);
}

void MiniMap::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF inner = imageRect();
    QPainterPath shape;
    shape.addRoundedRect(inner, 6, 6);
    painter.setClipPath(shape);
    if (!m_image.isNull()) painter.drawImage(inner, m_image);
    else painter.fillRect(inner, palette().color(QPalette::Base));
    const QRectF window = windowRect();
    QColor dim = palette().color(QPalette::Window);
    dim.setAlpha(160);
    QPainterPath outside = shape;
    if (!window.isEmpty()) {
        QPainterPath hole;
        hole.addRect(window);
        outside = outside.subtracted(hole);
    }
    painter.fillPath(outside, dim);
    if (!window.isEmpty()) {
        painter.setClipping(false);
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(window.adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

void MiniMap::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    m_dragging = true;
    m_last = event->position();
    setCursor(Qt::ClosedHandCursor);
    setFocus(Qt::MouseFocusReason);
    event->accept();
}

void MiniMap::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || m_content.isEmpty()) return;
    const QPointF delta = event->position() - m_last;
    m_last = event->position();
    const QRectF inner = imageRect();
    emit dragged(QPointF(delta.x() * m_content.width() / inner.width(),
                         delta.y() * m_content.height() / inner.height()));
    event->accept();
}

void MiniMap::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    mouseMoveEvent(event);
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    emit dragFinished(false);
    event->accept();
}

void MiniMap::wheelEvent(QWheelEvent *event) {
    emit wheelZoom(event->angleDelta().y() / 120.0);
    event->accept();
}

void MiniMap::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        emit dragFinished(true);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

}

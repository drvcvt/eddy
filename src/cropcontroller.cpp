#include "cropcontroller.h"
#include "selectionhandles.h"
#include <QLineF>
#include <cmath>

namespace eddy {
void CropController::begin(QSize bounds, QRect initial, int grid) {
    m_bounds = QRectF(QPointF(), bounds);
    m_grid = qMax(1, grid);
    m_active = bounds.width() >= m_grid && bounds.height() >= m_grid;
    m_role = -1;
    m_ratio = 0;
    m_rect = initial.isEmpty() ? m_bounds : QRectF(initial).intersected(m_bounds);
    emit changed();
}

QRect CropController::pixels() const {
    if (m_rect == m_bounds) return m_bounds.toRect();
    auto snap = [this](qreal value) { return qRound(value / m_grid) * m_grid; };
    const int maxX = int(m_bounds.width()) / m_grid * m_grid;
    const int maxY = int(m_bounds.height()) / m_grid * m_grid;
    if (maxX < m_grid || maxY < m_grid) return {};
    const int x = qBound(0, snap(m_rect.left()), maxX - m_grid);
    const int y = qBound(0, snap(m_rect.top()), maxY - m_grid);
    return {x, y, qBound(m_grid, snap(m_rect.right()) - x, maxX - x),
                  qBound(m_grid, snap(m_rect.bottom()) - y, maxY - y)};
}

QVector<QPointF> CropController::handles() const {
    const auto c = m_rect.center();
    return {m_rect.topLeft(), {c.x(), m_rect.top()}, m_rect.topRight(),
            {m_rect.right(), c.y()}, m_rect.bottomRight(), {c.x(), m_rect.bottom()},
            m_rect.bottomLeft(), {m_rect.left(), c.y()}};
}

int CropController::hit(QPointF point, qreal tolerance) const {
    const auto points = handles();
    for (int i = 0; i < points.size(); ++i)
        if (QLineF(points[i], point).length() <= tolerance) return i;
    return m_rect.contains(point) ? 8 : 9; // Move / draw a new selection.
}

Qt::CursorShape CropController::cursor(QPointF point, qreal tolerance) const {
    switch (hit(point, tolerance)) {
    case 0: case 4: return Qt::SizeFDiagCursor;
    case 2: case 6: return Qt::SizeBDiagCursor;
    case 1: case 5: return Qt::SizeVerCursor;
    case 3: case 7: return Qt::SizeHorCursor;
    case 8: return dragging() ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
    default: return Qt::CrossCursor;
    }
}

void CropController::assign(QRectF rect) {
    if (rect.width() < m_grid || rect.height() < m_grid || rect == m_rect) return;
    m_rect = rect;
    emit changed();
}

QRectF CropController::contained(QRectF rect, QPointF anchor, bool preserveAspect) const {
    if (!preserveAspect) return rect.intersected(m_bounds);
    qreal scale = 1;
    if (rect.left() < 0) scale = qMin(scale, anchor.x() / (anchor.x() - rect.left()));
    if (rect.top() < 0) scale = qMin(scale, anchor.y() / (anchor.y() - rect.top()));
    if (rect.right() > m_bounds.right())
        scale = qMin(scale, (m_bounds.right() - anchor.x()) / (rect.right() - anchor.x()));
    if (rect.bottom() > m_bounds.bottom())
        scale = qMin(scale, (m_bounds.bottom() - anchor.y()) / (rect.bottom() - anchor.y()));
    return QRectF(anchor + (rect.topLeft() - anchor) * scale,
                  anchor + (rect.bottomRight() - anchor) * scale);
}

void CropController::press(QPointF point, qreal tolerance, Qt::KeyboardModifiers modifiers) {
    if (!m_active) return;
    m_role = hit(point, tolerance);
    m_before = m_dragRect = m_rect;
    m_pointer = point;
    m_modifiers = modifiers;
    if (m_role == 9) {
        m_pointer = {qBound(0.0, point.x(), m_bounds.right()),
                     qBound(0.0, point.y(), m_bounds.bottom())};
    }
}

void CropController::move(QPointF point, Qt::KeyboardModifiers modifiers) {
    if (!dragging()) return;
    if (modifiers != m_modifiers && m_role == 9 && m_rect != m_before) {
        m_role = point.y() >= m_pointer.y()
            ? (point.x() >= m_pointer.x() ? 4 : 6)
            : (point.x() >= m_pointer.x() ? 2 : 0);
    }
    if (modifiers != m_modifiers && m_role != 9) {
        // Rebase before applying a modifier change, so the rectangle never jumps.
        m_dragRect = m_rect;
        m_pointer = point;
        m_modifiers = modifiers;
    }
    const QPointF delta = point - m_pointer;
    if (m_role == 8) {
        const QPointF offset(qBound(-m_dragRect.left(), delta.x(), m_bounds.right() - m_dragRect.right()),
                             qBound(-m_dragRect.top(), delta.y(), m_bounds.bottom() - m_dragRect.bottom()));
        assign(m_dragRect.translated(offset));
        return;
    }
    const bool aspect = m_ratio > 0 || modifiers.testFlag(Qt::ShiftModifier);
    const bool centered = modifiers.testFlag(Qt::AltModifier);
    if (m_role == 9) {
        QPointF extent = delta;
        const qreal ratio = m_ratio > 0 ? m_ratio : 1;
        if (aspect) extent.setY(std::copysign(qAbs(extent.x()) / ratio, extent.y()));
        assign(contained(QRectF(centered ? m_pointer - extent : m_pointer,
                               m_pointer + extent).normalized(), m_pointer, aspect || centered));
        return;
    }
    const auto rect = m_rect;
    m_rect = m_dragRect;
    const auto points = handles();
    m_rect = rect;
    const QPointF pointer = points[m_role] + delta;
    const auto effective = aspect ? modifiers | Qt::ShiftModifier : modifiers;
    const QRectF resized = resizedRect(m_dragRect, m_role, pointer, effective);
    const QPointF anchor = centered ? m_dragRect.center() : points[(m_role + 4) % 8];
    assign(contained(resized, anchor, aspect || centered));
}

void CropController::release() { m_role = -1; }
void CropController::cancelDrag() {
    if (!dragging()) return;
    m_role = -1;
    assign(m_before);
}
void CropController::nudge(QPointF delta) {
    delta.setX(qBound(-m_rect.left(), delta.x(), m_bounds.right() - m_rect.right()));
    delta.setY(qBound(-m_rect.top(), delta.y(), m_bounds.bottom() - m_rect.bottom()));
    assign(m_rect.translated(delta));
}
void CropController::setRatio(qreal ratio) {
    if (!std::isfinite(ratio) || ratio < 0) return;
    release();
    m_ratio = ratio;
    if (ratio <= 0) return;
    QSizeF size = m_rect.size();
    if (size.width() / size.height() > ratio) size.setWidth(size.height() * ratio);
    else size.setHeight(size.width() / ratio);
    assign(QRectF(m_rect.center() - QPointF(size.width(), size.height()) / 2, size));
}
void CropController::reset() {
    release();
    m_ratio = 0;
    assign(m_bounds);
}
void CropController::accept() {
    if (!m_active) return;
    release();
    m_active = false;
    emit changed();
    emit accepted(pixels());
}
void CropController::cancel() {
    if (!m_active) return;
    release();
    m_active = false;
    emit changed();
    emit cancelled();
}
}

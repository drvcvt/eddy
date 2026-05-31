#include "items/arrowitem.h"
#include <QPainter>
#include <QtMath>

namespace eddy {

ArrowItem::ArrowItem(const QPointF &start, const QPointF &end)
    : m_start(start), m_end(end) {}

void ArrowItem::setStart(const QPointF &start) { prepareGeometryChange(); m_start = start; update(); }
void ArrowItem::setEnd(const QPointF &end) { prepareGeometryChange(); m_end = end; update(); }

QRectF ArrowItem::boundingRect() const {
    const double pad = m_width * 4 + 4; // room for the head
    return QRectF(m_start, m_end).normalized().adjusted(-pad, -pad, pad, pad);
}

void ArrowItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(m_stroke, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->drawLine(m_start, m_end);

    const double angle = std::atan2(m_end.y() - m_start.y(), m_end.x() - m_start.x());
    const double head = m_width * 3.5 + 6.0;
    const double spread = M_PI / 7.0;
    QPointF h1 = m_end - QPointF(std::cos(angle - spread) * head, std::sin(angle - spread) * head);
    QPointF h2 = m_end - QPointF(std::cos(angle + spread) * head, std::sin(angle + spread) * head);
    QPainterPath path;
    path.moveTo(m_end); path.lineTo(h1); path.lineTo(h2); path.closeSubpath();
    p->setBrush(m_stroke);
    p->drawPath(path);
}

}

#include "items/redactitem.h"
#include <QPainter>
namespace eddy {
RedactItem::RedactItem(const QRectF &r) : m_rect(r.normalized()) { m_stroke = Qt::black; }
void RedactItem::setRect(const QRectF &r) { prepareGeometryChange(); m_rect = r.normalized(); update(); }
QRectF RedactItem::boundingRect() const { return m_rect; }
void RedactItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setPen(Qt::NoPen);
    p->setBrush(m_stroke);
    p->drawRect(m_rect);
}
}

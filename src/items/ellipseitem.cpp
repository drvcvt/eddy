#include "items/ellipseitem.h"
#include <QPainter>
namespace eddy {
EllipseItem::EllipseItem(const QRectF &r) : m_rect(r.normalized()) {}
void EllipseItem::setRect(const QRectF &r) { prepareGeometryChange(); m_rect = r.normalized(); update(); }
QRectF EllipseItem::boundingRect() const { const double p = m_width + 2; return m_rect.adjusted(-p,-p,p,p); }
void EllipseItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(QPen(m_stroke, m_width));
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(m_rect);
}
}

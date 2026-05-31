#include "items/rectitem.h"
#include <QPainter>
namespace eddy {
RectItem::RectItem(const QRectF &r) : m_rect(r.normalized()) {}
void RectItem::setRect(const QRectF &r) { prepareGeometryChange(); m_rect = r.normalized(); update(); }
QRectF RectItem::boundingRect() const { const double p = m_width + 2; return m_rect.adjusted(-p,-p,p,p); }
void RectItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(QPen(m_stroke, m_width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    p->setBrush(Qt::NoBrush);
    p->drawRect(m_rect);
}
}

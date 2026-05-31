#include "items/highlightitem.h"
#include <QPainter>
namespace eddy {
HighlightItem::HighlightItem(const QRectF &r) : m_rect(r.normalized()) {
    m_stroke = QColor(255, 235, 59); // yellow marker default
}
void HighlightItem::setRect(const QRectF &r) { prepareGeometryChange(); m_rect = r.normalized(); update(); }
QRectF HighlightItem::boundingRect() const { return m_rect.adjusted(-1,-1,1,1); }
void HighlightItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    QColor c = m_stroke; c.setAlpha(110);
    p->setBrush(c);
    p->setCompositionMode(QPainter::CompositionMode_Multiply);
    p->drawRect(m_rect);
}
}

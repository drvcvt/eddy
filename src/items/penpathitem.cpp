#include "items/penpathitem.h"
#include <QPainter>
namespace eddy {
PenPathItem::PenPathItem(const QPointF &start) { m_pts << start; }
void PenPathItem::addPoint(const QPointF &p) { prepareGeometryChange(); m_pts << p; update(); }
PenPathItem *PenPathItem::clone() const {
    auto *copy = new PenPathItem(m_pts.first());
    for (qsizetype i = 1; i < m_pts.size(); ++i) copy->addPoint(m_pts[i]);
    copy->setStrokeColor(m_stroke);
    copy->setStrokeWidth(m_width);
    return copy;
}
QRectF PenPathItem::boundingRect() const {
    const double pad = m_width + 2;
    return m_pts.boundingRect().adjusted(-pad,-pad,pad,pad);
}
void PenPathItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(QPen(m_stroke, m_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath path;
    if (!m_pts.isEmpty()) {
        path.moveTo(m_pts.first());
        for (int i = 1; i < m_pts.size(); ++i) path.lineTo(m_pts[i]);
    }
    p->drawPath(path);
}
}

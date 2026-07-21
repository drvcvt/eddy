#include "items/spotlightitem.h"
#include <QPainter>
#include <QPainterPath>

namespace eddy {

SpotlightItem::SpotlightItem(const QRectF &region, const QSizeF &canvasSize)
    : m_region(region.normalized()), m_canvasSize(canvasSize) {
    setZValue(-600);
}

void SpotlightItem::setRect(const QRectF &rect) {
    prepareGeometryChange();
    m_region = rect.normalized();
    update();
}

SpotlightItem *SpotlightItem::clone() const {
    auto *copy = new SpotlightItem(m_region, m_canvasSize);
    copy->setSpotlightShape(m_shape);
    copy->setIntensity(m_intensity);
    return copy;
}

QRectF SpotlightItem::boundingRect() const {
    return QRectF(-pos(), m_canvasSize);
}

QPainterPath SpotlightItem::holePath() const {
    QPainterPath path;
    if (m_shape == SpotlightShape::Ellipse) path.addEllipse(m_region);
    else path.addRoundedRect(m_region, 14, 14);
    return path;
}

QPainterPath SpotlightItem::shape() const { return holePath(); }

void SpotlightItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    QPainterPath dim;
    dim.addRect(boundingRect());
    dim.addPath(holePath());
    dim.setFillRule(Qt::OddEvenFill);
    static const int alpha[] = {70, 120, 170};
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0, 0, 0, alpha[m_intensity - 1]));
    p->drawPath(dim);
}

}

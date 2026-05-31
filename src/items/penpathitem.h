#pragma once
#include "annotationitem.h"
#include <QPolygonF>
namespace eddy {
class PenPathItem : public AnnotationItem {
public:
    explicit PenPathItem(const QPointF &start);
    void addPoint(const QPointF &p);
    int pointCount() const { return m_pts.size(); }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
private:
    QPolygonF m_pts;
};
}

#pragma once
#include "annotationitem.h"
#include <QPointF>

namespace eddy {

// Arrow defined by start->end in item-local coordinates.
class ArrowItem : public AnnotationItem {
public:
    ArrowItem(const QPointF &start, const QPointF &end);
    void setStart(const QPointF &start);
    void setEnd(const QPointF &end);
    QPointF start() const { return m_start; }
    QPointF end() const { return m_end; }

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    QPointF m_start, m_end;
};

}

#pragma once
#include "annotationitem.h"
namespace eddy {
class EllipseItem : public AnnotationItem {
public:
    explicit EllipseItem(const QRectF &r);
    void setRect(const QRectF &r);
    QRectF rect() const { return m_rect; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
private:
    QRectF m_rect;
};
}

#pragma once
#include "annotationitem.h"
namespace eddy {
class RectItem : public AnnotationItem {
public:
    explicit RectItem(const QRectF &r);
    void setRect(const QRectF &r) override;
    QRectF rect() const override { return m_rect; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
private:
    QRectF m_rect;
};
}

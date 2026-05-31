#pragma once
#include "annotationitem.h"
namespace eddy {
class HighlightItem : public AnnotationItem {
public:
    explicit HighlightItem(const QRectF &r);
    void setRect(const QRectF &r) override;
    QRectF rect() const override { return m_rect; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
private:
    QRectF m_rect;
};
}

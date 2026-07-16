#pragma once
#include "annotationitem.h"
#include <QSizeF>
namespace eddy {
enum class SpotlightShape { RoundedRect, Ellipse };
class SpotlightItem : public AnnotationItem {
public:
    SpotlightItem(const QRectF &region, const QSizeF &canvasSize);
    QRectF rect() const override { return m_region; }
    void setRect(const QRectF &rect) override;
    SpotlightItem *clone() const override;
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override;
    SpotlightShape spotlightShape() const { return m_shape; }
    void setSpotlightShape(SpotlightShape shape) { m_shape = shape; update(); }
    int intensity() const { return m_intensity; }
    void setIntensity(int intensity) { m_intensity = qBound(1, intensity, 3); update(); }
private:
    QPainterPath holePath() const;
    QRectF m_region;
    QSizeF m_canvasSize;
    SpotlightShape m_shape = SpotlightShape::RoundedRect;
    int m_intensity = 2;
};
}

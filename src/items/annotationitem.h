#pragma once
#include <QGraphicsItem>
#include <QColor>
#include <QRectF>

namespace eddy {

// Common style + a tag so the controller/exporter can reason about items.
class AnnotationItem : public QGraphicsItem {
public:
    enum { Type = QGraphicsItem::UserType + 1 };
    int type() const override { return Type; }

    void setStrokeColor(const QColor &c) { m_stroke = c; update(); }
    QColor strokeColor() const { return m_stroke; }
    void setStrokeWidth(double w) { prepareGeometryChange(); m_width = w; update(); }
    double strokeWidth() const { return m_width; }

    virtual QRectF rect() const { return QRectF(); }     // overridden by rect-shaped items
    virtual void setRect(const QRectF &) {}              // no-op for arrow/pen

protected:
    QColor m_stroke = QColor("#ff3b30");
    double m_width = 4.0;
};

}

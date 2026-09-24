#pragma once
#include <QGraphicsItem>
#include <QColor>
#include <QRectF>
#include <algorithm>
#include <optional>
#include <utility>

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
    virtual AnnotationItem *clone() const = 0;

    // Redactions and spotlights may show for a stretch of a video only (studio
    // plan 6.7): [from, to) in source ms; empty is the whole clip.
    using TimeWindow = std::optional<std::pair<qint64, qint64>>;
    static constexpr qint64 kMinWindowMs = 100;
    // The latest start that still leaves a window before `durationMs`.
    static qint64 windowStart(qint64 ms, qint64 durationMs) {
        return std::clamp<qint64>(ms, 0, std::max<qint64>(0, durationMs - kMinWindowMs));
    }
    TimeWindow timeWindow() const { return m_window; }
    void setTimeWindow(TimeWindow window) { m_window = window; }

protected:
    QColor m_stroke = QColor("#ff3b30");
    double m_width = 4.0;
    TimeWindow m_window;
};

}

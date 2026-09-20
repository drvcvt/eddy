#include "items/arrowitem.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QImage>
#include <QtMath>

namespace eddy {

ArrowItem::ArrowItem(const QPointF &start, const QPointF &end)
    : m_start(start), m_end(end) {}

void ArrowItem::setStart(const QPointF &start) { prepareGeometryChange(); m_start = start; update(); }
void ArrowItem::setEnd(const QPointF &end) { prepareGeometryChange(); m_end = end; update(); }

ArrowItem *ArrowItem::clone() const {
    auto *copy = new ArrowItem(m_start, m_end);
    copy->setStrokeColor(m_stroke);
    copy->setStrokeWidth(m_width);
    return copy;
}

QRectF ArrowItem::boundingRect() const {
    const double pad = m_width * 4 + 4; // room for the head
    return QRectF(m_start, m_end).normalized().adjusted(-pad, -pad, pad, pad);
}

void ArrowItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath shaft;
    shaft.moveTo(m_start);
    shaft.lineTo(m_end);
    QPainterPathStroker stroker;
    stroker.setWidth(m_width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);

    const double angle = std::atan2(m_end.y() - m_start.y(), m_end.x() - m_start.x());
    const double head = m_width * 3.5 + 6.0;
    const double spread = M_PI / 7.0;
    QPointF h1 = m_end - QPointF(std::cos(angle - spread) * head, std::sin(angle - spread) * head);
    QPointF h2 = m_end - QPointF(std::cos(angle + spread) * head, std::sin(angle + spread) * head);
    QPainterPath path;
    path.moveTo(m_end); path.lineTo(h1); path.lineTo(h2); path.closeSubpath();
    // Paint one silhouette: overlapping shaft/head passes otherwise darken
    // translucent arrows and accumulate coverage at their antialiased edges.
    path = path.united(stroker.createStroke(path)).united(stroker.createStroke(shaft));
    const QTransform transform = p->deviceTransform();
    const qreal scale = qMax(std::hypot(transform.m11(), transform.m12()),
                            std::hypot(transform.m21(), transform.m22()));
    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (invertible && scale > 0) {
        const QRectF visible = inverse.mapRect(QRectF(0, 0, p->device()->width(), p->device()->height()));
        const QRectF bounds = path.boundingRect().adjusted(-2 / scale, -2 / scale, 2 / scale, 2 / scale)
            .intersected(visible);
        if (bounds.isEmpty()) return;
        const QSize pixels(qCeil(bounds.width() * scale * 2), qCeil(bounds.height() * scale * 2));
        // Local 2x supersampling smooths shallow diagonals without resampling
        // the underlying screenshot. Bound temporary memory for large exports.
        if (qint64(pixels.width()) * pixels.height() <= 16 * 1024 * 1024) {
            QImage layer(pixels, QImage::Format_ARGB32_Premultiplied);
            if (!layer.isNull()) {
                layer.fill(Qt::transparent);
                QPainter sample(&layer);
                sample.setRenderHint(QPainter::Antialiasing);
                sample.scale(pixels.width() / bounds.width(), pixels.height() / bounds.height());
                sample.translate(-bounds.topLeft());
                sample.fillPath(path, m_stroke);
                sample.end();
                p->save();
                p->setRenderHint(QPainter::SmoothPixmapTransform);
                p->drawImage(bounds, layer);
                p->restore();
                return;
            }
        }
    }
    p->fillPath(path, m_stroke);
}

}

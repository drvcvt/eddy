#include "snapping.h"
#include "items/arrowitem.h"
#include "items/penpathitem.h"
#include "items/spotlightitem.h"
#include "items/stepitem.h"
#include <QGraphicsItem>
#include <algorithm>
#include <numeric>

namespace eddy {

QRectF alignmentBounds(const QGraphicsItem *item) {
    if (auto *arrow = dynamic_cast<const ArrowItem *>(item))
        return item->mapRectToScene(QRectF(arrow->start(), arrow->end()).normalized());
    if (auto *pen = dynamic_cast<const PenPathItem *>(item))
        return item->mapRectToScene(pen->points().boundingRect());
    if (auto *step = dynamic_cast<const StepItem *>(item))
        return item->mapRectToScene(step->shape().boundingRect());
    if (auto *annotation = dynamic_cast<const AnnotationItem *>(item); annotation && !annotation->rect().isNull())
        return item->mapRectToScene(annotation->rect());
    return item->sceneBoundingRect();
}

qreal Snapper::axis(Lock &lock, qreal a0, qreal a1, qreal a2, const QVector<QRectF> &targets, bool horizontal,
                    qreal catchDistance, qreal releaseDistance) {
    const qreal anchors[] = {a0, a1, a2};
    if (lock.on) {
        const qreal d = lock.value - anchors[lock.anchor];
        if (std::abs(d) <= releaseDistance) return d;
        lock.on = false;
    }
    qreal best = catchDistance;
    for (const QRectF &t : targets) {
        const qreal lines[] = {horizontal ? t.left() : t.top(), horizontal ? t.center().x() : t.center().y(),
                               horizontal ? t.right() : t.bottom()};
        for (int a = 0; a < 3; ++a)
            for (qreal line : lines) {
                const qreal d = line - anchors[a];
                if (std::abs(d) < best || (!lock.on && std::abs(d) <= best)) {
                    best = std::abs(d);
                    lock = {true, a, line, t};
                }
            }
    }
    return lock.on ? lock.value - anchors[lock.anchor] : 0.0;
}

QPointF Snapper::snap(const QRectF &moving, const QVector<QRectF> &targets, qreal catchDistance, qreal releaseDistance) {
    const QPointF offset(axis(m_x, moving.left(), moving.center().x(), moving.right(), targets, true,
                              catchDistance, releaseDistance),
                         axis(m_y, moving.top(), moving.center().y(), moving.bottom(), targets, false,
                              catchDistance, releaseDistance));
    // A guide runs along the shared line, across both boxes.
    const QRectF placed = moving.translated(offset);
    m_guides.clear();
    if (m_x.on)
        m_guides.append(QLineF(m_x.value, std::min(placed.top(), m_x.target.top()),
                               m_x.value, std::max(placed.bottom(), m_x.target.bottom())));
    if (m_y.on)
        m_guides.append(QLineF(std::min(placed.left(), m_y.target.left()), m_y.value,
                               std::max(placed.right(), m_y.target.right()), m_y.value));
    return offset;
}

void Snapper::reset() {
    m_x = {};
    m_y = {};
    m_guides.clear();
}

QVector<QPointF> alignDeltas(const QVector<QRectF> &boxes, Align align) {
    QRectF all;
    for (const QRectF &b : boxes) all |= b;
    QVector<QPointF> out;
    for (const QRectF &b : boxes) {
        switch (align) {
        case Align::Left: out.append({all.left() - b.left(), 0}); break;
        case Align::HCenter: out.append({all.center().x() - b.center().x(), 0}); break;
        case Align::Right: out.append({all.right() - b.right(), 0}); break;
        case Align::Top: out.append({0, all.top() - b.top()}); break;
        case Align::VCenter: out.append({0, all.center().y() - b.center().y()}); break;
        case Align::Bottom: out.append({0, all.bottom() - b.bottom()}); break;
        }
    }
    return out;
}

QVector<QPointF> distributeDeltas(const QVector<QRectF> &boxes, Qt::Orientation orientation) {
    if (boxes.size() < 3) return {};
    const bool h = orientation == Qt::Horizontal;
    auto start = [h](const QRectF &r) { return h ? r.left() : r.top(); };
    auto length = [h](const QRectF &r) { return h ? r.width() : r.height(); };
    // Along the axis; the index keeps equal starts in a stable order.
    QVector<int> order(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return start(boxes[a]) < start(boxes[b]); });
    const QRectF &first = boxes[order.first()];
    qreal end = start(first) + length(first), lengths = 0;
    for (int i : order) {
        end = std::max(end, start(boxes[i]) + length(boxes[i]));
        lengths += length(boxes[i]);
    }
    const qreal gap = (end - start(first) - lengths) / (boxes.size() - 1);
    if (gap < 0) return {};
    QVector<QPointF> out(boxes.size());
    qreal at = start(first);
    for (int i : order) {
        const qreal d = at - start(boxes[i]);
        out[i] = h ? QPointF(d, 0) : QPointF(0, d);
        at += length(boxes[i]) + gap;
    }
    return out;
}

}

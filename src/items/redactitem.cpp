#include "items/redactitem.h"
#include "items/rasteritem.h"   // redactBlur
#include <QPainter>

namespace eddy {

static const QColor kRedactFill("#0a0a0a");

RedactItem::RedactItem(RedactMode mode, const QImage &source, const QRectF &region)
    : m_mode(mode), m_source(source), m_region(region.normalized()) {
    setZValue(-500);
    setFlag(ItemSendsGeometryChanges, true);
    rebuildCache();
}

RedactItem *RedactItem::clone() const {
    auto *copy = new RedactItem(m_mode, m_source, m_region);
    copy->setTextRects(m_textRects);
    copy->setDetecting(m_detecting);
    copy->setStrokeColor(m_stroke);
    copy->setStrokeWidth(m_width);
    return copy;
}

void RedactItem::setRect(const QRectF &r) {
    prepareGeometryChange();
    m_region = r.normalized();
    if (isOcr(m_mode)) m_detecting = true;   // cover whole region until re-detect narrows it
    rebuildCache();
    update();
}

void RedactItem::setMode(RedactMode m) {
    if (m_mode == m) return;
    m_mode = m;
    rebuildCache();   // builds the blur cache for blur modes; clears it otherwise
    update();
}

void RedactItem::setSource(const QImage &source) {
    m_source = source;
    rebuildCache();
    update();
}

QVector<QRect> RedactItem::blurRectsInScene() const {
    if (!isBlur(m_mode)) return {};

    QVector<QRect> rects;
    for (const QRectF &rect : coverRects()) {
        const QRect sceneRect = mapRectToScene(rect).toAlignedRect().intersected(m_source.rect());
        if (!sceneRect.isEmpty()) rects.append(sceneRect);
    }
    return rects;
}

// Text rects only control *where* we draw; the blur cache covers the whole region,
// so no rebuild is needed here. Empty rects => OCR modes cover nothing (safe default).
void RedactItem::setTextRects(const QVector<QRectF> &rects) { m_textRects = rects; update(); }

void RedactItem::setDetecting(bool detecting) {
    if (m_detecting == detecting) return;
    m_detecting = detecting;
    update();
}

QRectF RedactItem::boundingRect() const { return m_region; }

void RedactItem::rebuildCache() {
    if (!isBlur(m_mode)) { m_cache = QImage(); m_cacheRect = QRect(); return; }
    // Sample from the SCENE area the cover currently occupies, so a moved cover
    // blurs the content actually under it (not its original position).
    m_cacheRect = mapRectToScene(m_region).toAlignedRect().intersected(m_source.rect());
    if (m_cacheRect.isEmpty()) { m_cache = QImage(); return; }
    m_cache = redactBlur(m_source.copy(m_cacheRect));
}

QVector<QRectF> RedactItem::coverRects() const {
    // Non-OCR, or an OCR mode still detecting: cover the WHOLE region — never expose
    // source while detection is pending. Once detection completes, narrow to text rects.
    if (!isOcr(m_mode) || m_detecting || m_textRects.isEmpty()) return { m_region };
    QVector<QRectF> out;
    for (const QRectF &tr : m_textRects) {
        const QRectF c = tr.intersected(m_region);
        if (!c.isEmpty()) out.append(c);
    }
    return out;
}

void RedactItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setPen(Qt::NoPen);
    const QVector<QRectF> rects = coverRects();
    if (isBlur(m_mode)) {
        if (m_cache.isNull()) return;
        for (const QRectF &r : rects) {
            p->save();
            p->setClipRect(r);
            p->drawImage(mapFromScene(QPointF(m_cacheRect.topLeft())), m_cache);
            p->restore();
        }
    } else {
        p->setBrush(kRedactFill);
        for (const QRectF &r : rects) p->drawRect(r);
    }
}

QVariant RedactItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemPositionHasChanged) {
        if (isOcr(m_mode)) m_detecting = true;   // stale text rects after a move -> cover whole (fail-safe)
        rebuildCache();                          // re-sample the blur at the new scene position
        update();
    }
    return QGraphicsItem::itemChange(change, value);
}

}

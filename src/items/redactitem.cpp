#include "items/redactitem.h"
#include "items/rasteritem.h"   // redactBlur
#include <QPainter>

namespace eddy {

static const QColor kRedactFill("#0a0a0a");

RedactItem::RedactItem(RedactMode mode, const QImage &source, const QRectF &region)
    : m_mode(mode), m_source(source), m_region(region.normalized()) {
    setZValue(-500);
    rebuildCache();
}

void RedactItem::setRect(const QRectF &r) {
    prepareGeometryChange();
    m_region = r.normalized();
    rebuildCache();
    update();
}

void RedactItem::setMode(RedactMode m) {
    if (m_mode == m) return;
    m_mode = m;
    if (isBlur(m_mode)) rebuildCache();
    update();
}

void RedactItem::setTextRects(const QVector<QRectF> &rects) { m_textRects = rects; update(); }

QRectF RedactItem::boundingRect() const { return m_region; }

void RedactItem::rebuildCache() {
    if (!isBlur(m_mode)) { m_cache = QImage(); m_cacheRect = QRect(); return; }
    m_cacheRect = m_region.toRect().intersected(m_source.rect());
    if (m_cacheRect.isEmpty()) { m_cache = QImage(); return; }
    m_cache = redactBlur(m_source.copy(m_cacheRect));
}

QVector<QRectF> RedactItem::coverRects() const {
    return { m_region };
}

void RedactItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setPen(Qt::NoPen);
    const QVector<QRectF> rects = coverRects();
    if (isBlur(m_mode)) {
        if (m_cache.isNull()) return;
        for (const QRectF &r : rects) {
            p->save();
            p->setClipRect(r);
            p->drawImage(m_cacheRect.topLeft(), m_cache);
            p->restore();
        }
    } else {
        p->setBrush(kRedactFill);
        for (const QRectF &r : rects) p->drawRect(r);
    }
}

}

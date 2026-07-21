#pragma once
#include "annotationitem.h"
#include <QImage>
#include <QVariant>
#include <QVector>
#include <QRectF>

namespace eddy {

enum class RedactMode { Blur, Blacken, OcrBlur, OcrBlacken };

// Unified redaction item. Covers its region (or, in OCR modes, only the supplied
// text rects) with a strong blur or a near-black fill. A rect-shaped AnnotationItem
// so the existing 8-handle resize works. Sits at zValue -500 — above the background
// pixmap (-1000), below all annotations (>=0) — so arrows/shapes stay visible on top.
class RedactItem : public AnnotationItem {
public:
    RedactItem(RedactMode mode, const QImage &source, const QRectF &region);

    void setRect(const QRectF &r) override;        // updates region; rebuilds blur cache
    QRectF rect() const override { return m_region; }
    RedactItem *clone() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

    RedactMode mode() const { return m_mode; }
    void setMode(RedactMode m);
    void setSource(const QImage &source);
    QVector<QRect> blurRectsInScene() const;
    QVector<QRectF> textRects() const { return m_textRects; }
    void setTextRects(const QVector<QRectF> &rects);

    bool isDetecting() const { return m_detecting; }
    void setDetecting(bool detecting);

    static bool isOcr(RedactMode m)  { return m == RedactMode::OcrBlur || m == RedactMode::OcrBlacken; }
    static bool isBlur(RedactMode m) { return m == RedactMode::Blur    || m == RedactMode::OcrBlur; }

private:
    void rebuildCache();
    QVector<QRectF> coverRects() const;   // region (or text rects clipped to region, in OCR modes)

    RedactMode m_mode;
    QImage m_source;        // full background in scene coords (shared, copy-on-write)
    QRectF m_region;
    QVector<QRectF> m_textRects;
    bool m_detecting = false;
    QImage m_cache;         // blurred crop of the region (blur modes only)
    QRect m_cacheRect;      // integer scene rect the cache covers
};

}

#pragma once
#include "annotationitem.h"
#include <QImage>
namespace eddy {

// Pure helpers (testable without a scene).
QImage boxBlur(const QImage &src, int radius);
QImage pixelate(const QImage &src, int block);

class RasterItem : public AnnotationItem {
public:
    enum Mode { Blur, Pixelate };
    RasterItem(Mode mode, const QImage &source, const QRectF &region);
    void setRegion(const QRectF &r);
    QRectF region() const { return m_region; }
    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
private:
    void rebuildCache();
    Mode m_mode;
    QImage m_source;     // the full composite/background in scene coords
    QRectF m_region;
    QImage m_cache;      // rasterized region
    QRect  m_cacheRect;  // integer region the cache covers
};
}

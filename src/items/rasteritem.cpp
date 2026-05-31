#include "items/rasteritem.h"
#include <QPainter>

namespace eddy {

QImage boxBlur(const QImage &srcIn, int radius) {
    if (radius < 1) return srcIn;
    QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);
    QImage dst = src;
    const int w = src.width(), h = src.height();
    // separable two-pass box blur (horizontal then vertical)
    auto pass = [&](const QImage &in, bool horizontal) {
        QImage out(in.size(), QImage::Format_ARGB32);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                long r=0,g=0,b=0,a=0; int n=0;
                for (int k=-radius; k<=radius; ++k) {
                    int sx = horizontal ? x+k : x;
                    int sy = horizontal ? y : y+k;
                    if (sx<0||sy<0||sx>=w||sy>=h) continue;
                    QRgb px = in.pixel(sx,sy);
                    r+=qRed(px); g+=qGreen(px); b+=qBlue(px); a+=qAlpha(px); ++n;
                }
                out.setPixel(x,y, qRgba(r/n,g/n,b/n,a/n));
            }
        }
        return out;
    };
    dst = pass(src, true);
    dst = pass(dst, false);
    return dst;
}

QImage pixelate(const QImage &srcIn, int block) {
    if (block < 2) return srcIn;
    QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);
    QImage dst = src;
    const int w = src.width(), h = src.height();
    for (int by=0; by<h; by+=block) {
        for (int bx=0; bx<w; bx+=block) {
            long r=0,g=0,b=0,a=0; int n=0;
            for (int y=by; y<qMin(by+block,h); ++y)
                for (int x=bx; x<qMin(bx+block,w); ++x) {
                    QRgb px = src.pixel(x,y);
                    r+=qRed(px); g+=qGreen(px); b+=qBlue(px); a+=qAlpha(px); ++n;
                }
            QRgb avg = qRgba(r/n,g/n,b/n,a/n);
            for (int y=by; y<qMin(by+block,h); ++y)
                for (int x=bx; x<qMin(bx+block,w); ++x)
                    dst.setPixel(x,y, avg);
        }
    }
    return dst;
}

RasterItem::RasterItem(Mode mode, const QImage &source, const QRectF &region)
    : m_mode(mode), m_source(source), m_region(region.normalized()) { rebuildCache(); }

void RasterItem::setRegion(const QRectF &r) {
    prepareGeometryChange(); m_region = r.normalized(); rebuildCache(); update();
}

QRectF RasterItem::boundingRect() const { return m_region; }

void RasterItem::rebuildCache() {
    m_cacheRect = m_region.toRect().intersected(m_source.rect());
    if (m_cacheRect.isEmpty()) { m_cache = QImage(); return; }
    QImage crop = m_source.copy(m_cacheRect);
    m_cache = (m_mode == Blur) ? boxBlur(crop, 8) : pixelate(crop, 12);
}

void RasterItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    if (!m_cache.isNull())
        p->drawImage(m_cacheRect.topLeft(), m_cache);
}

}

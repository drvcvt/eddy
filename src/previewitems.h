#pragma once
#include <QGraphicsPixmapItem>
#include <QGraphicsVideoItem>
#include <QElapsedTimer>
#include <QImage>
#include <QVideoFrame>

namespace eddy {

// Draws `image` into `target`. A view that shrinks it gets an area-filtered
// copy at the exact device size, cached until the image or that size changes:
// bilinear sampling reads only 2x2 texels and turns text and dithering into
// moiré below ~50%. Enlarged views blend below 2x and show crisp pixels from 2x on.
class DownscaledImage {
public:
    void draw(QPainter *painter, const QRectF &target, const QImage &image);
private:
    qint64 m_key = 0;
    QImage m_scaled;
};

// Background pixmap (image document, paused video still) drawn as above;
// SmoothTransformation also makes enlarged views bilinear.
class PreviewPixmapItem : public QGraphicsPixmapItem {
public:
    using QGraphicsPixmapItem::QGraphicsPixmapItem;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;
private:
    DownscaledImage m_image;
};

// Playback frames drawn as above, enlarged like the still;
// Qt's own paint samples the nearest texel at any zoom. Each frame is
// converted once, and no more often than the screen refreshes: a 240 fps
// recording would otherwise convert and filter four frames per refresh.
// Eddy sizes the item to the frame, so the aspect-ratio mode is not applied,
// and a visible child (the paused still) is taken to cover the whole frame.
class PreviewVideoItem : public QGraphicsVideoItem {
public:
    using QGraphicsVideoItem::QGraphicsVideoItem;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
private:
    DownscaledImage m_image;
    QVideoFrame m_shown;          // the frame m_frameImage was converted from
    QImage m_frameImage;
    QElapsedTimer m_converted;
    bool m_refreshQueued = false;
};

}

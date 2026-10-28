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
    // `moving`: under the zoom effect's camera the picture goes to its exact, fractional
    // place, filtered from the nearest halving, so it glides with the camera instead of
    // stepping by whole pixels, being shrunk again for every step or turning to squares.
    void draw(QPainter *painter, const QRectF &target, const QImage &image, bool moving = false);
    // The device size draw() shrinks `image` to under `device`; empty when it does not shrink.
    // Moving, the nearest halving at or above it, which stays put through a ramp.
    static QSize shrunkSize(const QTransform &device, const QRectF &target, QSize image, bool moving = false);
    // `image` area-filtered to `size`, as draw() would; any thread.
    static QImage shrunk(const QImage &image, QSize size);
private:
    qint64 m_key = 0;
    QImage m_scaled;
    qint64 m_halvedKey = 0;
    QImage m_halved;
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
// While playing, conversion and filtering run on a pool thread.
// Eddy sizes the item to the frame, so the aspect-ratio mode is not applied,
// and a visible child (the paused still) is taken to cover the whole frame.
class PreviewVideoItem : public QGraphicsVideoItem {
public:
    explicit PreviewVideoItem(QGraphicsItem *parent = nullptr);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
private:
    DownscaledImage m_image;
    bool m_converting = false;
    QVideoFrame m_shown;          // the frame m_frameImage was converted from
    QImage m_frameImage;
    QElapsedTimer m_converted;
    bool m_refreshQueued = false;
};

}

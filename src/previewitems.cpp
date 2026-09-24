#include "previewitems.h"
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QVideoFrame>
#include <QVideoSink>

namespace eddy {

// A 2x2 box average: an exact area filter for a factor of two, and cheap.
// An odd last row or column is repeated.
static QImage halve(const QImage &src) {
    QImage dst((src.width() + 1) / 2, (src.height() + 1) / 2, src.format());
    const int lastX = src.width() - 1, lastY = src.height() - 1;
    for (int y = 0; y < dst.height(); ++y) {
        const auto *a = reinterpret_cast<const quint32 *>(src.constScanLine(2 * y));
        const auto *b = reinterpret_cast<const quint32 *>(src.constScanLine(qMin(2 * y + 1, lastY)));
        auto *d = reinterpret_cast<quint32 *>(dst.scanLine(y));
        for (int x = 0; x < dst.width(); ++x) {
            const int x1 = qMin(2 * x + 1, lastX);
            const quint32 p = a[2 * x], q = a[x1], r = b[2 * x], s = b[x1];
            // Two 8-bit channels per 32-bit lane, each with room for the sum of four.
            const quint32 rb = ((p & 0xff00ff) + (q & 0xff00ff) + (r & 0xff00ff) + (s & 0xff00ff) + 0x20002) >> 2;
            const quint32 ag = (((p >> 8) & 0xff00ff) + ((q >> 8) & 0xff00ff) + ((r >> 8) & 0xff00ff)
                                + ((s >> 8) & 0xff00ff) + 0x20002) >> 2;
            d[x] = (rb & 0xff00ff) | ((ag & 0xff00ff) << 8);
        }
    }
    return dst;
}

// Area-filtered copy at `size`: exact halvings first, so Qt's area filter
// only takes the last step between 1/2 and 1 on a quarter of the pixels or less.
static QImage shrink(QImage image, QSize size) {
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    while (image.width() >= 2 * size.width() && image.height() >= 2 * size.height()) image = halve(image);
    return image.size() == size ? image : image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void DownscaledImage::draw(QPainter *painter, const QRectF &target, const QImage &image) {
    if (image.isNull()) return;
    const QTransform device = painter->deviceTransform();
    const QRectF mapped = device.mapRect(target);
    if (device.type() > QTransform::TxScale
        || mapped.width() > image.width() + 0.5 || mapped.height() > image.height() + 0.5) {
        // Enlarged: blended below 2x, where square pixels would come out one or
        // two device pixels wide and bend text; crisp squares from 2x on.
        painter->save();
        if (mapped.width() >= 2 * image.width() && mapped.height() >= 2 * image.height())
            painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter->drawImage(target, image);
        painter->restore();
        return;
    }
    // Whole device pixels keep 100% an exact copy and the cached size stable while panning.
    const QPoint topLeft(qRound(mapped.left()), qRound(mapped.top()));
    const QSize size(qRound(mapped.right()) - topLeft.x(), qRound(mapped.bottom()) - topLeft.y());
    if (size.isEmpty()) return;
    if (m_key != image.cacheKey() || m_scaled.size() != size) {
        m_scaled = shrink(image, size);
        if (m_scaled.devicePixelRatio() != 1) m_scaled.setDevicePixelRatio(1);
        m_key = image.cacheKey();
    }
    painter->save();
    painter->setWorldTransform(device.inverted() * painter->worldTransform());   // device pixels
    painter->drawImage(topLeft, m_scaled);
    painter->restore();
}

void PreviewPixmapItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    const QPixmap pixmap = this->pixmap();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, transformationMode() == Qt::SmoothTransformation);
    m_image.draw(painter, QRectF(offset(), pixmap.deviceIndependentSize()), pixmap.toImage());
}

void PreviewVideoItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    // A visible child is the paused still, which covers the frame: skip filtering twice.
    for (const QGraphicsItem *child : childItems())
        if (child->isVisible()) return;
    const QVideoFrame frame = videoSink()->videoFrame();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // toImage() leaves the presentation transform to Qt's own paint.
    if (frame.rotation() != QtVideo::Rotation::None || frame.mirrored()) {
        QGraphicsVideoItem::paint(painter, option, widget);
        return;
    }
#else
    Q_UNUSED(option); Q_UNUSED(widget);
#endif
    if (frame != m_shown) {
        const QScreen *screen = widget ? widget->screen() : nullptr;
        // A little under one refresh period, so a 60 fps video on a 60 Hz screen keeps every frame.
        const qint64 interval = qint64(900.0 / qMax(1.0, screen ? screen->refreshRate() : 60.0));
        const qint64 since = m_converted.isValid() ? m_converted.elapsed() : interval;
        // Judged in video time, which arrives without the event loop's jitter; a
        // seek or loop going backwards always converts.
        const qint64 step = m_shown.isValid() && frame.startTime() >= 0 && m_shown.startTime() >= 0
            ? (frame.startTime() - m_shown.startTime()) / 1000 : interval;
        if (step < 0 || step >= interval || since >= 4 * interval) {
            m_shown = frame;
            m_frameImage = frame.isValid() ? frame.toImage() : QImage();
            m_converted.restart();
        } else if (!m_refreshQueued) {
            // Too soon after the last one: show that, and come back for the newest.
            m_refreshQueued = true;
            QTimer::singleShot(interval - since, this, [this] {
                m_refreshQueued = false;
                update();
            });
        }
    }
    m_image.draw(painter, boundingRect(), m_frameImage);
}

}

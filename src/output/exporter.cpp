#include "output/exporter.h"
#include "items/annotationitem.h"
#include <QPainter>
#include <QFile>
#include <QBuffer>

namespace eddy {

QImage renderToImage(QGraphicsScene &scene, const QSize &nativeSize) {
    QImage img(nativeSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF target(QPointF(0,0), QSizeF(nativeSize));
    // A camera's item cache (Canvas::cacheForCamera) is a scaled copy: the export draws
    // those items directly. A device cache is only ever used on screen.
    QList<QGraphicsItem *> cached;
    for (QGraphicsItem *item : scene.items())
        if (item->cacheMode() == QGraphicsItem::ItemCoordinateCache && item->data(AnnotationItem::kCameraCacheKey).isValid()) {
            item->setCacheMode(QGraphicsItem::NoCache);
            cached.append(item);
        }
    scene.render(&p, target, QRectF(0,0,nativeSize.width(),nativeSize.height()));
    for (QGraphicsItem *item : std::as_const(cached))
        item->setCacheMode(QGraphicsItem::ItemCoordinateCache, item->data(AnnotationItem::kCameraCacheKey).toSize());
    p.end();
    return img;
}

QByteArray encodePng(const QImage &img) {
    QByteArray b; QBuffer buf(&b); buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return b;
}

DeliverResult writePng(const QImage &img, const QString &pathOrDash) {
    DeliverResult r;
    const QByteArray png = encodePng(img);
    if (png.isEmpty()) { r.error = "failed to encode PNG"; return r; }
    if (pathOrDash == "-") {
        QFile out;
        if (!out.open(stdout, QIODevice::WriteOnly)) { r.error = "cannot open stdout"; return r; }
        if (out.write(png) != png.size() || !out.flush()) { r.error = "write to stdout failed"; return r; }
        r.ok = true; return r;
    }
    QFile f(pathOrDash);
    if (!f.open(QIODevice::WriteOnly)) { r.error = "cannot write " + pathOrDash + ": " + f.errorString(); return r; }
    if (f.write(png) != png.size() || !f.flush()) { r.error = "write failed: " + f.errorString(); return r; }
    r.ok = true; return r;
}

}

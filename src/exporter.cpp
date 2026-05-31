#include "exporter.h"
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
    scene.render(&p, target, QRectF(0,0,nativeSize.width(),nativeSize.height()));
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
    if (pathOrDash == "-") {
        QFile out;
        if (!out.open(stdout, QIODevice::WriteOnly)) { r.error = "cannot open stdout"; return r; }
        out.write(png); out.flush(); r.ok = true; return r;
    }
    QFile f(pathOrDash);
    if (!f.open(QIODevice::WriteOnly)) { r.error = "cannot write " + pathOrDash + ": " + f.errorString(); return r; }
    f.write(png); r.ok = true; return r;
}

}

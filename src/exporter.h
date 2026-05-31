#pragma once
#include <QImage>
#include <QGraphicsScene>
#include <QByteArray>
#include <QString>

namespace eddy {

// Renders at 1:1 native resolution. The caller MUST set
// scene.setSceneRect(QRectF(QPointF(), nativeSize)) before calling, or the
// source rect will not match the scene and the output will be scaled/cropped.
QImage renderToImage(QGraphicsScene &scene, const QSize &nativeSize);

QByteArray encodePng(const QImage &img);

struct DeliverResult { bool ok=false; QString error; };

// Writes PNG to a file path ("-" = stdout). Returns ok/error.
DeliverResult writePng(const QImage &img, const QString &pathOrDash);

}

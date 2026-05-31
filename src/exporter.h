#pragma once
#include <QImage>
#include <QGraphicsScene>
#include "config.h"

namespace eddy {

// Render the scene (background + annotations) at native pixel size.
QImage renderToImage(QGraphicsScene &scene, const QSize &nativeSize);

QByteArray encodePng(const QImage &img);

struct DeliverResult { bool ok=false; QString error; };

// Writes PNG to a file path ("-" = stdout). Returns ok/error.
DeliverResult writePng(const QImage &img, const QString &pathOrDash);

}

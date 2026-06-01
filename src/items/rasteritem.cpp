#include "items/rasteritem.h"

namespace eddy {

QImage redactBlur(const QImage &src) {
    if (src.isNull()) return src;
    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width(), h = in.height();
    const int dw = qMax(1, w / 12);
    const int dh = qMax(1, h / 12);
    const QImage small = in.scaled(dw, dh, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return small.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

}

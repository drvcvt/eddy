#include "items/regions/rasteritem.h"

namespace eddy {

QImage redactBlur(const QImage &src) {
    if (src.isNull()) return src;
    // Only the small image: stretching it back is the painter's job, at draw time, so
    // neither a move nor a new video frame pays for a full-size scale.
    return src.scaled(qMax(1, src.width() / 12), qMax(1, src.height() / 12),
                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

}

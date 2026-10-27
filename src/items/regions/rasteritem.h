#pragma once
#include <QImage>
namespace eddy {
// Strong, unreadable blur for redaction: the image at 1/12 of its size, to be
// drawn stretched over the region with smooth sampling.
QImage redactBlur(const QImage &src);
}

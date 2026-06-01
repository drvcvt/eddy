#pragma once
#include <QImage>
namespace eddy {
// Strong, unreadable blur for redaction: downscale then smooth-upscale.
QImage redactBlur(const QImage &src);
}

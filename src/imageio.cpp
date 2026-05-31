#include "imageio.h"
#include <QFile>
#include <QImageReader>
#include <QBuffer>
#include <cstdio>

namespace eddy {

LoadResult loadImageBytes(const QByteArray &bytes) {
    LoadResult r;
    if (bytes.isEmpty()) { r.error = "empty image data"; return r; }
    QBuffer buf; buf.setData(bytes); buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    QImage img = reader.read();
    if (img.isNull()) { r.error = "could not decode image: " + reader.errorString(); return r; }
    r.ok = true; r.image = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return r;
}

static QByteArray readAllStdin() {
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return {};
    return in.readAll();
}

LoadResult loadInput(const InputSpec &spec) {
    if (spec.kind == InputSpec::Stdin)
        return loadImageBytes(readAllStdin());
    QFile f(spec.path);
    if (!f.open(QIODevice::ReadOnly)) {
        LoadResult r; r.error = "cannot open " + spec.path + ": " + f.errorString();
        return r;
    }
    return loadImageBytes(f.readAll());
}

}

#pragma once
#include <QImage>
#include "cli.h"

namespace eddy {

struct LoadResult {
    bool ok = false;
    QImage image;
    QString error;
};

LoadResult loadInput(const InputSpec &spec);   // file or stdin
LoadResult loadImageBytes(const QByteArray &bytes);

}

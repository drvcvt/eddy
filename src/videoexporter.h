#pragma once
#include <QImage>
#include <QString>
#include "exporter.h"

namespace eddy {

struct VideoExportRequest {
    QString inputPath;
    QString outputPath;
    QImage overlay;
};

// Re-encodes the source video with a static transparent overlay. Audio is kept
// for mp4/mov/mkv outputs when ffmpeg can stream-copy it.
DeliverResult writeVideoWithOverlay(const VideoExportRequest &req);

}

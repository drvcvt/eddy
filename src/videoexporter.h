#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <atomic>
#include <memory>
#include "exporter.h"

namespace eddy {

struct VideoExportRequest {
    QString inputPath;
    QString outputPath;
    QImage overlay;
    qint64 trimInMs = 0;
    qint64 trimOutMs = -1;
    int timeoutMs = 30 * 60 * 1000;
    std::shared_ptr<std::atomic_bool> cancelRequested;
    QVector<QRect> blurRects;
};

DeliverResult replaceFileAtomically(const QString &from, const QString &to);

// Re-encodes the source video with a static transparent overlay. Audio is kept
// for mp4/mov/mkv outputs when ffmpeg can stream-copy it.
DeliverResult writeVideoWithOverlay(const VideoExportRequest &req);

}

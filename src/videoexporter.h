#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>
#include "exporter.h"
#include "studiodocument.h"
#include "studiostyle.h"

namespace eddy {

struct VideoExportRequest {
    QString inputPath;
    QString outputPath;
    QImage overlay;
    qint64 trimInMs = 0;
    qint64 trimOutMs = -1;
    int timeoutMs = 30 * 60 * 1000;
    // An encoder whose output position does not advance this long is killed;
    // a stalled hardware encoder falls back to the CPU encoder.
    int stallTimeoutMs = 60 * 1000;
    QVector<QRect> blurRects;
    QRect cropRect;
    StudioStyle studio;   // framing around the (cropped) video; off by default
    // Zoom segments in source time. Any at all send the export through the
    // frame renderer (renderexport.h) instead of the filter graph.
    QVector<ZoomSegment> zooms;
    // Called from the export thread with 0-99 as encoding advances, or -1
    // while the output length is unknown.
    std::function<void(int percent)> progress;
    // Polled from the export thread; returning true kills ffmpeg and fails
    // the export with "video export cancelled".
    std::function<bool()> cancelled;
};

DeliverResult replaceFileAtomically(const QString &from, const QString &to);

// Re-encodes the source video with a static transparent overlay. Audio is kept
// for mp4/mov/mkv outputs when ffmpeg can stream-copy it.
DeliverResult writeVideoWithOverlay(const VideoExportRequest &req);
// The same export, always drawn frame by frame at 60 fps; used for zooms and
// by tests comparing both paths.
DeliverResult writeVideoRendered(const VideoExportRequest &req);

}

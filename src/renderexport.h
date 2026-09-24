#pragma once
#include <QImage>
#include <QSize>
#include <QStringList>
#include <functional>
#include "exporter.h"

namespace eddy {

// Decode -> Qt -> encode through two ffmpeg pipes. Decoder and encoder each run
// on their own thread and the caller's thread draws, so the three overlap;
// done one after another they miss real time at 1080p60 (studio plan 2).
struct RenderPipeline {
    QString ffmpeg;
    QStringList decodeArgs;     // must write rawvideo bgra frames of sourceSize to stdout
    QSize sourceSize;
    QStringList encodeArgs;     // must read rawvideo bgra frames of outputSize from stdin
    QSize outputSize;
    qint64 expectedFrames = 0;  // for progress; 0 while unknown
    // Draws output frame `index` from the decoded `source`.
    std::function<void(qint64 index, const QImage &source, QImage &output)> render;
    int timeoutMs = -1;
    // Nothing reaching the encoder this long stops the export.
    int stallTimeoutMs = 60 * 1000;
    std::function<void(int percent)> progress;   // 0-99, or -1 while unknown
    std::function<bool()> cancelled;
};

// Errors read like the filter-graph export's: "video export cancelled",
// "ffmpeg export timed out", "ffmpeg stopped making progress", or ffmpeg's own
// last line of stderr.
DeliverResult runRenderPipeline(const RenderPipeline &pipeline);

}

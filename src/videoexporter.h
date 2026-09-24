#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
#include "cursortrack.h"
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
    // "Always keep zoomed in" (studio plan 6.5): the unzoomed view inside the
    // crop, shaped like the output frame. Null: the whole crop.
    QRect baseView;
    // Export presets (studio plan 6.9): the framed output shrunk to this
    // shorter side (0: full size) and capped at this frame rate. A `.gif`
    // output path makes a paletted GIF without sound.
    int maxShortSide = 0;
    int maxFps = 60;
    // Split, cut and sped-up stretches of the source (studio plan 6.6); empty
    // keeps the whole trimmed range at 1x.
    QVector<Fragment> fragments;
    // Boltsnap's pointer track: Cursor zooms follow it, and the base view does
    // when `baseFollowsCursor` (studio plan 6.2 and 6.5).
    std::shared_ptr<const CursorTrack> cursorTrack;
    bool baseFollowsCursor = false;
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

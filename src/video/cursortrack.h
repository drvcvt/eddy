#pragma once
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>
#include <optional>

namespace eddy {

// Pointer data recorded next to a video (docs/specs/2026-09-23-studio-mode.md).
// Times are milliseconds from the video's first frame, positions are display
// pixels of that video. A hidden sample means the pointer left the capture.
struct CursorSample {
    qint64 ms = 0;
    QPointF pos;
    bool visible = true;
};

struct CursorClick {
    qint64 ms = 0;
    int button = 1;   // 1 left, 2 middle, 3 right
    bool down = true;
};

struct CursorTrack {
    QSize videoSize;
    bool cursorInVideo = true;   // false: the video is cursor-free
    // Absolute path of the frame-identical cursor-free video, when one exists.
    QString cleanVideoPath;
    QVector<CursorSample> samples;
    QVector<CursorClick> clicks;

    // Linear between visible samples, holding still across gaps over 50 ms
    // (Boltsnap only records moves); nothing before the first sample or while
    // the pointer is hidden.
    std::optional<QPointF> positionAt(qint64 ms) const;
};

struct CursorTrackResult {
    bool found = false;   // a sidecar exists; ok/error describe whether it was usable
    bool ok = false;
    CursorTrack track;
    QString error;
};

// `clip.mp4` -> `clip.cursor.json` in the same directory.
QString cursorTrackPathFor(const QString &videoPath);
// `videoPath` resolves `clean_video`; empty skips that lookup.
CursorTrackResult parseCursorTrack(const QByteArray &json, QSize videoSize,
                                   const QString &videoPath = {});
CursorTrackResult loadCursorTrack(const QString &videoPath, QSize videoSize);

}

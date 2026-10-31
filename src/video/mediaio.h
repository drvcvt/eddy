#pragma once
#include <QVector>
#include <functional>
#include <QImage>
#include <QSize>
#include <QString>
#include "app/cli.h"
#include "video/cursortrack.h"
#include <optional>

namespace eddy {

enum class MediaKind {
    Image,
    Video,
};

struct VideoInfo {
    QSize size;
    qint64 durationMs = 0;
    double fps = 0.0;
    bool cropSupported = true;
    bool hasAudio = false;
    // Where the first audio stream starts on the video's time axis.
    qint64 audioOffsetMs = 0;
    // 8-bit 4:2:0 (either range) in a codec NVDEC takes, not rotated: decoded on the GPU it
    // comes back exactly as ffmpeg's own decoder gives it (export-gpu plan).
    bool gpuDecodable = false;
};

// "m:ss" or "h:mm:ss", and with milliseconds; the playback bar's clocks.
QString formatTime(qint64 ms);
QString formatPreciseTime(qint64 ms);

struct MediaDocument {
    MediaKind kind = MediaKind::Image;
    QImage image;
    QString path;
    VideoInfo video;
    // Optional pointer data recorded next to the video; absent for most files.
    std::optional<CursorTrack> cursorTrack;

    QSize nativeSize() const;
};

struct ProbeVideoResult {
    bool ok = false;
    VideoInfo info;
    QString error;
};

struct LoadMediaResult {
    bool ok = false;
    MediaDocument document;
    QString error;
    QString warning;   // non-fatal, e.g. an unusable cursor track
};

bool pathLooksLikeVideo(const QString &path);
QString mediaMimeTypeForPath(const QString &path);
bool parseVideoTime(const QString &text, qint64 *milliseconds);
ProbeVideoResult probeVideoFile(const QString &path);
LoadMediaResult loadMediaInput(const InputSpec &spec);

// C2: stretches where the picture does not change, from ffmpeg's freezedetect on a small
// copy (10 fps, 320 px wide). Source ms; `fromMs` is where the analysis started.
QVector<std::pair<qint64, qint64>> parseFreezes(const QByteArray &ffmpegLog, qint64 fromMs, qint64 toMs);
// Runs it over [fromMs, toMs); nullopt when ffmpeg is missing, fails or `cancelled` says so.
std::optional<QVector<std::pair<qint64, qint64>>> detectFreezes(const QString &path, qint64 fromMs, qint64 toMs,
                                                               const std::function<bool()> &cancelled);

}

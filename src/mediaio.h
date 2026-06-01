#pragma once
#include <QImage>
#include <QSize>
#include <QString>
#include "cli.h"

namespace eddy {

enum class MediaKind {
    Image,
    Video,
};

struct VideoInfo {
    QSize size;
    qint64 durationMs = 0;
    double fps = 0.0;
};

struct MediaDocument {
    MediaKind kind = MediaKind::Image;
    QImage image;
    QString path;
    VideoInfo video;

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
};

bool pathLooksLikeVideo(const QString &path);
QString mediaMimeTypeForPath(const QString &path);
ProbeVideoResult probeVideoFile(const QString &path);
LoadMediaResult loadMediaInput(const InputSpec &spec);

}

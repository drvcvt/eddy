#include "mediaio.h"
#include "imageio.h"
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>

namespace eddy {

QSize MediaDocument::nativeSize() const {
    return kind == MediaKind::Video ? video.size : image.size();
}

static QString lowerSuffix(const QString &path) {
    return QFileInfo(path).suffix().toLower();
}

bool pathLooksLikeVideo(const QString &path) {
    const QString ext = lowerSuffix(path);
    if (ext == "mp4" || ext == "m4v" || ext == "mov" || ext == "webm"
        || ext == "mkv" || ext == "avi")
        return true;

    QMimeDatabase db;
    const auto mime = db.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    return mime.name().startsWith(QStringLiteral("video/"));
}

QString mediaMimeTypeForPath(const QString &path) {
    const QString ext = lowerSuffix(path);
    if (ext == "mp4" || ext == "m4v") return QStringLiteral("video/mp4");
    if (ext == "webm") return QStringLiteral("video/webm");
    if (ext == "mov") return QStringLiteral("video/quicktime");
    if (ext == "mkv") return QStringLiteral("video/x-matroska");
    if (ext == "avi") return QStringLiteral("video/x-msvideo");

    QMimeDatabase db;
    const auto mime = db.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
    if (mime.name().startsWith(QStringLiteral("video/"))
        || mime.name().startsWith(QStringLiteral("image/")))
        return mime.name();
    return QStringLiteral("application/octet-stream");
}

static double parseRate(const QString &rate) {
    const auto parts = rate.split('/');
    if (parts.size() == 2) {
        bool okNum = false, okDen = false;
        const double num = parts[0].toDouble(&okNum);
        const double den = parts[1].toDouble(&okDen);
        if (okNum && okDen && den != 0.0) return num / den;
    }
    bool ok = false;
    const double v = rate.toDouble(&ok);
    return ok ? v : 0.0;
}

ProbeVideoResult probeVideoFile(const QString &path) {
    ProbeVideoResult r;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        r.error = QStringLiteral("cannot open video ") + path;
        return r;
    }

    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty()) {
        r.error = QStringLiteral("ffprobe not found");
        return r;
    }

    QProcess p;
    p.start(ffprobe, {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-select_streams"), QStringLiteral("v:0"),
        QStringLiteral("-show_entries"), QStringLiteral("stream=width,height,r_frame_rate:format=duration"),
        QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1"),
        path
    });
    if (!p.waitForFinished(15000)) {
        p.kill();
        p.waitForFinished();
        r.error = QStringLiteral("ffprobe timed out for ") + path;
        return r;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        r.error = err.isEmpty() ? QStringLiteral("ffprobe failed for ") + path : err;
        return r;
    }

    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    int width = 0;
    int height = 0;
    double fps = 0.0;
    qint64 durationMs = 0;
    for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
        const int eq = line.indexOf('=');
        if (eq <= 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == "width") width = value.toInt();
        else if (key == "height") height = value.toInt();
        else if (key == "r_frame_rate") fps = parseRate(value);
        else if (key == "duration") {
            bool ok = false;
            const double seconds = value.toDouble(&ok);
            if (ok && seconds > 0.0) durationMs = qRound64(seconds * 1000.0);
        }
    }

    if (width <= 0 || height <= 0) {
        r.error = QStringLiteral("could not determine video dimensions for ") + path;
        return r;
    }
    r.ok = true;
    r.info.size = QSize(width, height);
    r.info.fps = fps;
    r.info.durationMs = durationMs;
    return r;
}

LoadMediaResult loadMediaInput(const InputSpec &spec) {
    LoadMediaResult r;
    if (spec.kind == InputSpec::Stdin) {
        auto img = loadInput(spec);
        if (!img.ok) { r.error = img.error; return r; }
        r.ok = true;
        r.document.kind = MediaKind::Image;
        r.document.image = img.image;
        return r;
    }

    if (pathLooksLikeVideo(spec.path)) {
        auto probe = probeVideoFile(spec.path);
        if (!probe.ok) { r.error = probe.error; return r; }
        r.ok = true;
        r.document.kind = MediaKind::Video;
        r.document.path = QFileInfo(spec.path).absoluteFilePath();
        r.document.video = probe.info;
        return r;
    }

    auto img = loadInput(spec);
    if (!img.ok) { r.error = img.error; return r; }
    r.ok = true;
    r.document.kind = MediaKind::Image;
    r.document.path = QFileInfo(spec.path).absoluteFilePath();
    r.document.image = img.image;
    return r;
}

ContactSheetResult generateVideoContactSheet(const QString &path, qint64 durationMs,
                                             int frameCount, QSize frameSize) {
    ContactSheetResult result;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty()) { result.error = QStringLiteral("ffmpeg not found"); return result; }
    if (durationMs <= 0 || frameCount <= 0 || frameSize.isEmpty()) {
        result.error = QStringLiteral("invalid contact sheet dimensions");
        return result;
    }

    const double fps = frameCount * 1000.0 / durationMs;
    const QString filter = QStringLiteral(
        "fps=%1,scale=%2:%3:force_original_aspect_ratio=decrease,"
        "pad=%2:%3:(ow-iw)/2:(oh-ih)/2,tile=%4x1")
        .arg(fps, 0, 'f', 6).arg(frameSize.width()).arg(frameSize.height()).arg(frameCount);
    QProcess process;
    process.start(ffmpeg, {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-i"), path, QStringLiteral("-vf"), filter,
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-f"), QStringLiteral("image2pipe"),
        QStringLiteral("-vcodec"), QStringLiteral("png"), QStringLiteral("pipe:1")
    });
    if (!process.waitForFinished(15000)) {
        process.kill(); process.waitForFinished();
        result.error = QStringLiteral("timeline preview timed out");
        return result;
    }
    const QByteArray png = process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || !result.image.loadFromData(png, "PNG")) {
        result.error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (result.error.isEmpty()) result.error = QStringLiteral("could not build timeline preview");
        return result;
    }
    result.ok = true;
    return result;
}

}

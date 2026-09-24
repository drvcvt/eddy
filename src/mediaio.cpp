#include "mediaio.h"
#include "imageio.h"
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QRegularExpression>
#include <limits>
#include <cmath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace eddy {

bool parseVideoTime(const QString &text, qint64 *milliseconds) {
    static const QRegularExpression syntax(QStringLiteral("^[0-9]+(?::[0-9]{1,2}){0,2}(?:\\.[0-9]{1,3})?$"));
    const QString value = text.trimmed();
    if (!milliseconds || !syntax.match(value).hasMatch()) return false;
    const auto decimal = value.split('.');
    const auto parts = decimal[0].split(':');
    qint64 seconds = 0;
    constexpr qint64 limit = std::numeric_limits<qint64>::max() / 1000 - 1;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const qint64 part = parts[i].toLongLong(&ok);
        if (!ok || (i > 0 && part >= 60) || part > limit || seconds > (limit - part) / 60)
            return false;
        seconds = seconds * 60 + part;
    }
    *milliseconds = seconds * 1000 + (decimal.size() == 2
        ? decimal[1].leftJustified(3, QLatin1Char('0')).toInt() : 0);
    return true;
}

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
        QStringLiteral("-show_entries"), QStringLiteral("stream=width,height,r_frame_rate,sample_aspect_ratio:stream_side_data=rotation,displaymatrix:format=duration"),
        QStringLiteral("-of"), QStringLiteral("json"),
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

    const auto metadata = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    const auto streams = metadata.value("streams").toArray();
    const auto stream = streams.isEmpty() ? QJsonObject() : streams.first().toObject();
    int width = stream.value("width").toInt();
    int height = stream.value("height").toInt();
    const double fps = parseRate(stream.value("r_frame_rate").toString());
    const double seconds = metadata.value("format").toObject().value("duration").toString().toDouble();
    const qint64 durationMs = std::isfinite(seconds) && seconds > 0
        && seconds < double(std::numeric_limits<qint64>::max() / 1000) ? qRound64(seconds * 1000) : 0;
    const QString sar = stream.value("sample_aspect_ratio").toString();
    const double aspect = parseRate(QString(sar).replace(':', '/'));
    if (aspect > 0 && std::isfinite(aspect) && width * aspect <= std::numeric_limits<int>::max())
        width = qRound(width * aspect);
    else if (!sar.isEmpty() && sar != "N/A" && sar != "0:1") r.info.cropSupported = false;
    for (const auto &value : stream.value("side_data_list").toArray()) {
        const auto data = value.toObject();
        if (!data.contains("rotation")) {
            if (data.contains("displaymatrix")) r.info.cropSupported = false;
            continue;
        }
        const double rotation = data.value("rotation").toDouble();
        if (!std::isfinite(rotation) || qAbs(std::remainder(rotation, 90.0)) > 0.01) {
            r.info.cropSupported = false;
            continue;
        }
        if (qAbs(std::remainder(rotation, 180.0)) > 1) std::swap(width, height);
        // Only unscaled, orthogonal rotations share Qt/ffmpeg display coordinates.
        // Mirroring, shear, translation and perspective need a separate mapping.
        QList<qint64> matrix;
        for (const auto &line : data.value("displaymatrix").toString().split('\n')) {
            if (!line.contains(':')) continue;
            for (const auto &number : line.section(':', 1).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
                matrix.append(number.toLongLong());
        }
        if (matrix.size() != 9) { r.info.cropSupported = false; continue; }
        const auto axis = [](qint64 n) { return n == 0 || n == 65536 || n == -65536; };
        r.info.cropSupported = r.info.cropSupported && axis(matrix[0]) && axis(matrix[1])
            && axis(matrix[3]) && axis(matrix[4]) && matrix[0] * matrix[4] - matrix[1] * matrix[3] == 4294967296LL
            && matrix[2] == 0 && matrix[5] == 0 && matrix[6] == 0 && matrix[7] == 0 && matrix[8] == 1073741824;
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
        auto cursor = loadCursorTrack(r.document.path, probe.info.size);
        if (cursor.ok) r.document.cursorTrack = std::move(cursor.track);
        else if (cursor.found) r.warning = QStringLiteral("ignoring cursor track: ") + cursor.error;
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

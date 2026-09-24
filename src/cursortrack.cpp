#include "cursortrack.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace eddy {

static constexpr int kCursorTrackVersion = 1;
// 240 Hz for an hour stays near 25 MB; anything far beyond is not a track.
static constexpr qint64 kMaxTrackBytes = 128 * 1024 * 1024;

std::optional<QPointF> CursorTrack::positionAt(qint64 ms) const {
    const auto after = std::upper_bound(samples.cbegin(), samples.cend(), ms,
        [](qint64 t, const CursorSample &s) { return t < s.ms; });
    if (after == samples.cbegin()) return std::nullopt;
    const CursorSample &a = *(after - 1);
    if (!a.visible) return std::nullopt;
    if (after == samples.cend() || !after->visible || after->ms == a.ms) return a.pos;
    const double f = double(ms - a.ms) / double(after->ms - a.ms);
    return a.pos + (after->pos - a.pos) * f;
}

QString cursorTrackPathFor(const QString &videoPath) {
    const QFileInfo info(videoPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".cursor.json"));
}

static CursorTrackResult fail(const QString &error) {
    CursorTrackResult r;
    r.found = true;
    r.error = error;
    return r;
}

// Times may carry fractions; ordering is checked on the raw value, since
// rounding 10.6 and 10.7 would otherwise make them look reversed. A step back
// of up to 1 ms (rounding at a segment seam) is clamped instead of failing
// the whole track.
static bool readTime(const QJsonValue &value, double *previous, qint64 *ms) {
    double t = value.toDouble(-1);
    if (!std::isfinite(t) || t < 0 || t > 1e12 || t < *previous - 1.0) return false;
    t = std::max(t, *previous);
    *previous = t;
    *ms = qint64(std::llround(t));
    return true;
}

CursorTrackResult parseCursorTrack(const QByteArray &json, QSize videoSize, const QString &videoPath) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("cursor track is not a JSON object: ") + parseError.errorString());
    const QJsonObject root = doc.object();
    if (root.value("format").toString() != QStringLiteral("boltsnap.cursor"))
        return fail(QStringLiteral("cursor track has an unknown format"));
    const int version = root.value("version").toInt();
    if (version < 1 || version > kCursorTrackVersion)
        return fail(QStringLiteral("cursor track version %1 is not supported").arg(version));

    CursorTrack track;
    track.videoSize = QSize(root.value("width").toInt(), root.value("height").toInt());
    // Positions are only meaningful in the pixel space they were recorded for.
    if (track.videoSize != videoSize)
        return fail(QStringLiteral("cursor track is for %1x%2, video is %3x%4")
            .arg(track.videoSize.width()).arg(track.videoSize.height())
            .arg(videoSize.width()).arg(videoSize.height()));
    track.cursorInVideo = root.value("cursor_in_video").toBool(true);
    // Only a sibling file: a sidecar must not point Eddy anywhere else on disk.
    const QString clean = root.value("clean_video").toString();
    if (!videoPath.isEmpty() && !clean.isEmpty() && QFileInfo(clean).fileName() == clean
        && clean != QStringLiteral("..")) {
        const QFileInfo cleanInfo(QFileInfo(videoPath).dir(), clean);
        if (cleanInfo.isFile()) track.cleanVideoPath = cleanInfo.absoluteFilePath();
    }

    double previous = 0;
    for (const QJsonValue &value : root.value("samples").toArray()) {
        const QJsonArray row = value.toArray();
        CursorSample s;
        if (row.size() < 3 || !readTime(row.at(0), &previous, &s.ms))
            return fail(QStringLiteral("cursor track has an invalid sample"));
        s.visible = !row.at(1).isNull() && !row.at(2).isNull();
        if (s.visible) {
            const double x = row.at(1).toDouble(NAN), y = row.at(2).toDouble(NAN);
            if (!std::isfinite(x) || !std::isfinite(y))
                return fail(QStringLiteral("cursor track has an invalid position"));
            s.pos = QPointF(x, y);
        }
        track.samples.append(s);
    }

    previous = 0;
    for (const QJsonValue &value : root.value("clicks").toArray()) {
        const QJsonArray row = value.toArray();
        CursorClick c;
        if (row.size() < 3 || !readTime(row.at(0), &previous, &c.ms))
            return fail(QStringLiteral("cursor track has an invalid click"));
        c.button = row.at(1).toInt();
        c.down = row.at(2).toInt() != 0;
        if (c.button < 1 || c.button > 3)
            return fail(QStringLiteral("cursor track has an invalid click button"));
        track.clicks.append(c);
    }

    CursorTrackResult r;
    r.found = r.ok = true;
    r.track = std::move(track);
    return r;
}

CursorTrackResult loadCursorTrack(const QString &videoPath, QSize videoSize) {
    QFile file(cursorTrackPathFor(videoPath));
    if (!file.exists()) return {};
    if (file.size() > kMaxTrackBytes)
        return fail(QStringLiteral("cursor track is too large"));
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot read cursor track: ") + file.errorString());
    return parseCursorTrack(file.readAll(), videoSize, videoPath);
}

}

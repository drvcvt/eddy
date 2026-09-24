#include "studiodocument.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>
#include <utility>

namespace eddy {

bool StudioDocument::timeVarying() const {
    return !zooms.isEmpty();
}

namespace {

template <typename E> using Name = std::pair<const char *, E>;
using B = StudioStyle::Background;
using Target = ZoomSegment::Target;
using Motion = ZoomSegment::Motion;
constexpr Name<B> kBackgrounds[] = {{"none", B::None}, {"color", B::Color},
                                    {"gradient", B::Gradient}, {"image", B::Image}};
constexpr Name<Target> kTargets[] = {{"point", Target::Point}, {"cursor", Target::Cursor}};
constexpr Name<Motion> kMotions[] = {{"focused", Motion::Focused}, {"smooth", Motion::Smooth},
                                     {"instant", Motion::Instant}};

template <typename E, std::size_t N>
QString nameOf(const Name<E> (&table)[N], E value) {
    for (const auto &[name, e] : table)
        if (e == value) return QString::fromLatin1(name);
    return QString::fromLatin1(table[0].first);
}

// Reads one block and keeps the first reason it is unusable.
struct Reader {
    QString error;

    bool fail(const QString &why) {
        if (error.isEmpty()) error = why;
        return false;
    }
    bool object(const QJsonObject &o, const char *key, QJsonObject *out) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isObject()) return fail(QStringLiteral("%1 must be an object").arg(QLatin1String(key)));
        *out = v.toObject();
        return true;
    }
    bool number(const QJsonObject &o, const char *key, double lo, double hi, double *out,
                std::optional<double> fallback = std::nullopt) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined() && fallback) { *out = *fallback; return true; }
        const double d = v.toDouble(std::nan(""));
        if (!v.isDouble() || !std::isfinite(d) || d < lo || d > hi)
            return fail(QStringLiteral("%1 must be a number from %2 to %3")
                            .arg(QLatin1String(key)).arg(lo).arg(hi));
        *out = d;
        return true;
    }
    bool whole(const QJsonObject &o, const char *key, double lo, double hi, qint64 *out) {
        double d = 0;
        if (!number(o, key, lo, hi, &d)) return false;
        if (d != std::floor(d)) return fail(QStringLiteral("%1 must be a whole number").arg(QLatin1String(key)));
        *out = qint64(d);
        return true;
    }
    bool flag(const QJsonObject &o, const char *key, bool *out) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) { *out = false; return true; }
        if (!v.isBool()) return fail(QStringLiteral("%1 must be true or false").arg(QLatin1String(key)));
        *out = v.toBool();
        return true;
    }
    bool point(const QJsonObject &o, const char *key, QSize bounds, QPointF *out) {
        const QJsonArray a = o.value(QLatin1String(key)).toArray();
        const double x = a.size() == 2 ? a[0].toDouble(-1) : -1;
        const double y = a.size() == 2 ? a[1].toDouble(-1) : -1;
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0
            || x > bounds.width() || y > bounds.height())
            return fail(QStringLiteral("%1 must be [x, y] inside the video").arg(QLatin1String(key)));
        *out = QPointF(x, y);
        return true;
    }
    template <typename E, std::size_t N>
    bool name(const QJsonObject &o, const char *key, const Name<E> (&table)[N], E *out) {
        const QJsonValue v = o.value(QLatin1String(key));
        for (const auto &[text, e] : table)
            if (v.toString() == QLatin1String(text)) { *out = e; return true; }
        return fail(QStringLiteral("unknown %1").arg(QLatin1String(key)));
    }

    bool style(const QJsonObject &o, StudioStyle *s) {
        if (!name(o, "background", kBackgrounds, &s->background)) return false;
        for (const auto &[key, color] : {std::pair{"color", &s->color}, std::pair{"color2", &s->color2}}) {
            const QColor c(o.value(QLatin1String(key)).toString());
            if (!c.isValid()) return fail(QStringLiteral("%1 is not a colour").arg(QLatin1String(key)));
            *color = c;
        }
        qint64 angle = 0;
        if (!whole(o, "angle", 0, 359, &angle) || !number(o, "padding", 0, 20, &s->padding)
            || !number(o, "radius", 0, 6, &s->radius) || !number(o, "shadow", 0, 100, &s->shadow))
            return false;
        s->gradientAngle = int(angle);
        const QJsonValue image = o.value(QLatin1String("image"));
        if (image.isString()) s->imagePath = image.toString();
        else if (!image.isNull() && !image.isUndefined()) return fail(QStringLiteral("image must be a path or null"));
        if (s->background == B::Image && s->imagePath.isEmpty())
            return fail(QStringLiteral("an image background needs an image"));
        const QJsonValue aspect = o.value(QLatin1String("aspect"));
        if (aspect.isArray()) {
            const QJsonArray a = aspect.toArray();
            const double w = a.size() == 2 ? a[0].toDouble(0) : 0, h = a.size() == 2 ? a[1].toDouble(0) : 0;
            if (w != std::floor(w) || h != std::floor(h) || w < 1 || h < 1 || w > 100 || h > 100)
                return fail(QStringLiteral("aspect must be two whole numbers from 1 to 100"));
            s->aspect = QSize(int(w), int(h));
        } else if (!aspect.isNull() && !aspect.isUndefined()) {
            return fail(QStringLiteral("aspect must be [w, h] or null"));
        }
        return true;
    }

    bool document(const QJsonObject &json, qint64 durationMs, QSize source, StudioDocument *d) {
        const QJsonValue version = json.value(QLatin1String("version"));
        if (!version.isDouble()) return fail(QStringLiteral("version is missing"));
        if (version.toDouble() != kStudioFormatVersion)
            return fail(QStringLiteral("unsupported Studio format version %1").arg(version.toDouble()));
        if (!json.value(QLatin1String("style")).isObject()) return fail(QStringLiteral("style is missing"));
        if (!style(json.value(QLatin1String("style")).toObject(), &d->style)) return false;

        const QJsonArray zooms = json.value(QLatin1String("zooms")).toArray();
        const QJsonArray fragments = json.value(QLatin1String("fragments")).toArray();
        if (zooms.size() > kStudioMaxItems || fragments.size() > kStudioMaxItems)
            return fail(QStringLiteral("more than %1 zooms or fragments").arg(kStudioMaxItems));
        if (durationMs <= 0 && (!zooms.isEmpty() || !fragments.isEmpty()))
            return fail(QStringLiteral("zooms and fragments need a video"));

        QSet<quint32> ids;
        qint64 lastEnd = 0;
        for (const QJsonValue &value : zooms) {
            const QJsonObject o = value.toObject();
            ZoomSegment z;
            qint64 id = 0;
            if (!whole(o, "id", 1, 4294967295.0, &id) || !whole(o, "start", 0, durationMs, &z.startMs)
                || !whole(o, "end", 0, durationMs, &z.endMs) || !number(o, "scale", 1.1, 4, &z.scale)
                || !name(o, "target", kTargets, &z.target) || !point(o, "point", source, &z.point)
                || !name(o, "motion", kMotions, &z.motion))
                return false;
            z.id = quint32(id);
            if (z.endMs <= z.startMs) return fail(QStringLiteral("a zoom ends before it starts"));
            if (z.startMs < lastEnd) return fail(QStringLiteral("zooms overlap or are out of order"));
            if (ids.contains(z.id)) return fail(QStringLiteral("zoom id %1 is used twice").arg(z.id));
            ids.insert(z.id);
            lastEnd = z.endMs;
            d->zooms.append(z);
        }

        bool kept = fragments.isEmpty();
        for (const QJsonValue &value : fragments) {
            const QJsonObject o = value.toObject();
            Fragment f;
            if (!whole(o, "start", 0, double(durationMs - 1), &f.startMs)
                || !number(o, "speed", 0.25, 4, &f.speed, 1.0) || !flag(o, "removed", &f.removed))
                return false;
            if (d->fragments.isEmpty() ? f.startMs != 0 : f.startMs <= d->fragments.last().startMs)
                return fail(QStringLiteral("fragments must start at 0 and keep increasing"));
            kept = kept || !f.removed;
            d->fragments.append(f);
        }
        if (!kept) return fail(QStringLiteral("every fragment is cut"));

        const QJsonValue audio = json.value(QLatin1String("audio"));
        if (!audio.isUndefined() && !audio.isBool()) return fail(QStringLiteral("audio must be true or false"));
        d->audio = audio.toBool(true);

        QJsonObject camera;
        if (!object(json, "camera", &camera)) return false;
        if (camera.contains(QLatin1String("motion")) && !name(camera, "motion", kMotions, &d->motion))
            return false;
        double blur = 0;
        if (!number(camera, "motionBlur", 0, 100, &blur, 0.0)) return false;
        d->motionBlur = int(blur);

        QJsonObject keep;
        if (!object(json, "keepZoomedIn", &keep) || !flag(keep, "on", &d->keepZoomedIn)) return false;
        if (keep.contains(QLatin1String("center")) && !point(keep, "center", source, &d->keepCenter))
            return false;
        const QJsonValue follows = keep.value(QLatin1String("followCursor"));
        if (!follows.isUndefined() && !follows.isBool()) return fail(QStringLiteral("followCursor must be true or false"));
        // Files from before cursor following keep the centre they were saved with.
        d->keepFollowsCursor = follows.toBool(!keep.contains(QLatin1String("center")));
        return true;
    }
};

}

QJsonObject studioToJson(const StudioDocument &doc) {
    const StudioStyle &s = doc.style;
    const QJsonObject style{
        {"background", nameOf(kBackgrounds, s.background)},
        {"color", s.color.name()},
        {"color2", s.color2.name()},
        {"angle", s.gradientAngle},
        {"image", s.imagePath.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(s.imagePath)},
        {"padding", s.padding},
        {"radius", s.radius},
        {"shadow", s.shadow},
        {"aspect", s.aspect.isEmpty() ? QJsonValue(QJsonValue::Null)
                                      : QJsonValue(QJsonArray{s.aspect.width(), s.aspect.height()})}};
    QJsonArray zooms;
    for (const ZoomSegment &z : doc.zooms)
        zooms.append(QJsonObject{
            {"id", qint64(z.id)}, {"start", z.startMs}, {"end", z.endMs}, {"scale", z.scale},
            {"target", nameOf(kTargets, z.target)}, {"point", QJsonArray{z.point.x(), z.point.y()}},
            {"motion", nameOf(kMotions, z.motion)}});
    QJsonArray fragments;
    for (const Fragment &f : doc.fragments) {
        QJsonObject o{{"start", f.startMs}};
        if (f.speed != 1.0) o[QLatin1String("speed")] = f.speed;
        if (f.removed) o[QLatin1String("removed")] = true;
        fragments.append(o);
    }
    return QJsonObject{
        {"version", kStudioFormatVersion},
        {"style", style},
        {"zooms", zooms},
        {"fragments", fragments},
        {"audio", doc.audio},
        {"camera", QJsonObject{{"motion", nameOf(kMotions, doc.motion)}, {"motionBlur", doc.motionBlur}}},
        {"keepZoomedIn", QJsonObject{{"on", doc.keepZoomedIn},
                                     {"center", QJsonArray{doc.keepCenter.x(), doc.keepCenter.y()}},
                                     {"followCursor", doc.keepFollowsCursor}}}};
}

std::optional<StudioDocument> studioFromJson(const QJsonObject &json, qint64 durationMs,
                                             QSize source, QString *error) {
    Reader reader;
    StudioDocument doc;
    doc.keepCenter = QPointF(source.width() / 2.0, source.height() / 2.0);
    const bool ok = reader.document(json, durationMs, source, &doc);
    if (error) *error = ok ? QString() : reader.error;
    if (!ok) return std::nullopt;
    return doc;
}

}

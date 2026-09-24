#include "projectcodec.h"
#include "items/arrowitem.h"
#include "items/ellipseitem.h"
#include "items/highlightitem.h"
#include "items/penpathitem.h"
#include "items/rectitem.h"
#include "items/redactitem.h"
#include "items/spotlightitem.h"
#include "items/textitem.h"
#include <QJsonDocument>
#include <cmath>

namespace eddy {

namespace {

constexpr int kMaxItems = 5000;
constexpr int kMaxPoints = 20000;         // per pen stroke
constexpr int kMaxTotalPoints = 200000;
constexpr int kMaxText = 100000;          // characters per text
constexpr double kMaxCoordinate = 1e6;

QJsonArray pointJson(QPointF p) { return {p.x(), p.y()}; }
QJsonArray rectJson(const QRectF &r) { return {r.x(), r.y(), r.width(), r.height()}; }

// Reads one entry and keeps the first reason it is unusable.
struct Reader {
    QString error;
    bool fail(const QString &why) {
        if (error.isEmpty()) error = why;
        return false;
    }
    bool number(const QJsonValue &v, double lo, double hi, double *out, const char *what) {
        const double d = v.toDouble(std::nan(""));
        if (!v.isDouble() || !std::isfinite(d) || d < lo || d > hi)
            return fail(QStringLiteral("%1 must be a number from %2 to %3").arg(QLatin1String(what)).arg(lo).arg(hi));
        *out = d;
        return true;
    }
    bool point(const QJsonValue &v, QPointF *out, const char *what) {
        const QJsonArray a = v.toArray();
        double x = 0, y = 0;
        if (!v.isArray() || a.size() != 2) return fail(QStringLiteral("%1 must be [x, y]").arg(QLatin1String(what)));
        if (!number(a[0], -kMaxCoordinate, kMaxCoordinate, &x, what)
            || !number(a[1], -kMaxCoordinate, kMaxCoordinate, &y, what)) return false;
        *out = QPointF(x, y);
        return true;
    }
    bool rect(const QJsonValue &v, QRectF *out, const char *what) {
        const QJsonArray a = v.toArray();
        double r[4];
        if (!v.isArray() || a.size() != 4) return fail(QStringLiteral("%1 must be [x, y, w, h]").arg(QLatin1String(what)));
        for (int i = 0; i < 4; ++i)
            if (!number(a[i], i < 2 ? -kMaxCoordinate : 0, kMaxCoordinate, &r[i], what)) return false;
        *out = QRectF(r[0], r[1], r[2], r[3]);
        return true;
    }
    bool color(const QJsonValue &v, QColor *out) {
        const QColor c(v.toString());
        if (!v.isString() || !c.isValid()) return fail(QStringLiteral("color is not a colour"));
        *out = c;
        return true;
    }
};

const std::pair<const char *, RedactMode> kRedactModes[] = {
    {"blur", RedactMode::Blur}, {"blacken", RedactMode::Blacken},
    {"ocrBlur", RedactMode::OcrBlur}, {"ocrBlacken", RedactMode::OcrBlacken}};

QString modeName(RedactMode mode) {
    for (const auto &[name, m] : kRedactModes)
        if (m == mode) return QLatin1String(name);
    return QStringLiteral("blur");
}

QString alignName(Qt::Alignment a) {
    return a.testFlag(Qt::AlignHCenter) ? QStringLiteral("center")
        : a.testFlag(Qt::AlignRight) ? QStringLiteral("right") : QStringLiteral("left");
}

}

QJsonArray itemsToJson(const QList<QGraphicsItem *> &itemsInStackingOrder) {
    QJsonArray out;
    for (QGraphicsItem *item : itemsInStackingOrder) {
        QJsonObject o{{"pos", pointJson(item->pos())}, {"z", item->zValue()}};
        if (auto *text = dynamic_cast<TextItem *>(item)) {
            const TextState s = text->state();
            o["type"] = "text";
            o["text"] = s.text;
            o["font"] = QJsonObject{{"family", s.font.family()}, {"size", s.font.pointSizeF()},
                                    {"bold", s.font.bold()}, {"italic", s.font.italic()}};
            o["color"] = s.color.name(QColor::HexArgb);
            o["textWidth"] = s.width;
            o["align"] = alignName(s.alignment);
            o["label"] = s.labelStyle == TextLabelStyle::Filled ? "filled" : "plain";
            out.append(o);
            continue;
        }
        auto *a = dynamic_cast<AnnotationItem *>(item);
        if (!a) continue;
        o["color"] = a->strokeColor().name(QColor::HexArgb);
        o["width"] = a->strokeWidth();
        if (auto *arrow = dynamic_cast<ArrowItem *>(item)) {
            o["type"] = "arrow";
            o["start"] = pointJson(arrow->start());
            o["end"] = pointJson(arrow->end());
        } else if (auto *pen = dynamic_cast<PenPathItem *>(item)) {
            o["type"] = "pen";
            QJsonArray points;
            for (const QPointF &p : pen->points()) points.append(pointJson(p));
            o["points"] = points;
        } else if (auto *redact = dynamic_cast<RedactItem *>(item)) {
            o["type"] = "redact";
            o["rect"] = rectJson(redact->rect());
            o["mode"] = modeName(redact->mode());
            QJsonArray rects;
            for (const QRectF &r : redact->textRects()) rects.append(rectJson(r));
            o["textRects"] = rects;
            o["detecting"] = redact->isDetecting();
        } else if (auto *spot = dynamic_cast<SpotlightItem *>(item)) {
            o["type"] = "spotlight";
            o["rect"] = rectJson(spot->rect());
            o["shape"] = spot->spotlightShape() == SpotlightShape::Ellipse ? "ellipse" : "rounded";
            o["intensity"] = spot->intensity();
        } else if (dynamic_cast<EllipseItem *>(item)) {
            o["type"] = "ellipse";
            o["rect"] = rectJson(a->rect());
        } else if (dynamic_cast<HighlightItem *>(item)) {
            o["type"] = "highlight";
            o["rect"] = rectJson(a->rect());
        } else if (dynamic_cast<RectItem *>(item)) {
            o["type"] = "rect";
            o["rect"] = rectJson(a->rect());
        } else {
            continue;
        }
        out.append(o);
    }
    return out;
}

std::optional<QList<QGraphicsItem *>> itemsFromJson(const QJsonArray &items, const QImage &background,
                                                   QSize canvas, QString *error) {
    Reader r;
    QList<QGraphicsItem *> made;
    auto give = [&](bool ok) -> std::optional<QList<QGraphicsItem *>> {
        if (error) *error = ok ? QString() : r.error;
        if (ok) return made;
        qDeleteAll(made);
        return std::nullopt;
    };
    if (items.size() > kMaxItems) {
        r.fail(QStringLiteral("more than %1 annotations").arg(kMaxItems));
        return give(false);
    }
    int totalPoints = 0;
    for (const QJsonValue &value : items) {
        const QJsonObject o = value.toObject();
        const QString type = o.value("type").toString();
        QPointF pos;
        double z = 0;
        if (!value.isObject() || !r.point(o.value("pos"), &pos, "pos")
            || !r.number(o.value("z"), -10000, 10000, &z, "z"))
            return give(false);
        QGraphicsItem *item = nullptr;
        if (type == QLatin1String("text")) {
            const QJsonObject f = o.value("font").toObject();
            TextState s;
            double size = 0, width = 0;
            if (!o.value("text").isString() || o.value("text").toString().size() > kMaxText)
                { r.fail(QStringLiteral("text is missing or too long")); return give(false); }
            if (!f.value("family").isString() || !r.number(f.value("size"), 1, 1000, &size, "font size")
                || !r.number(o.value("textWidth"), -1, kMaxCoordinate, &width, "textWidth")
                || !r.color(o.value("color"), &s.color))
                return give(false);
            s.text = o.value("text").toString();
            s.font.setFamily(f.value("family").toString());
            s.font.setPointSizeF(size);
            s.font.setBold(f.value("bold").toBool());
            s.font.setItalic(f.value("italic").toBool());
            s.width = width;
            const QString align = o.value("align").toString();
            s.alignment = align == QLatin1String("center") ? Qt::AlignHCenter
                : align == QLatin1String("right") ? Qt::AlignRight : Qt::AlignLeft;
            s.labelStyle = o.value("label").toString() == QLatin1String("filled") ? TextLabelStyle::Filled
                                                                                 : TextLabelStyle::Plain;
            auto *text = new TextItem(s.text, s.color, size);
            text->applyState(s);
            item = text;
        } else {
            QColor color;
            double width = 0;
            if (!r.color(o.value("color"), &color) || !r.number(o.value("width"), 0, 1000, &width, "width"))
                return give(false);
            AnnotationItem *a = nullptr;
            QRectF rect;
            const bool rectShaped = type == QLatin1String("rect") || type == QLatin1String("ellipse")
                || type == QLatin1String("highlight") || type == QLatin1String("redact")
                || type == QLatin1String("spotlight");
            if (rectShaped && !r.rect(o.value("rect"), &rect, "rect")) return give(false);
            if (type == QLatin1String("arrow")) {
                QPointF start, end;
                if (!r.point(o.value("start"), &start, "start") || !r.point(o.value("end"), &end, "end"))
                    return give(false);
                a = new ArrowItem(start, end);
            } else if (type == QLatin1String("pen")) {
                const QJsonArray points = o.value("points").toArray();
                totalPoints += points.size();
                if (points.isEmpty() || points.size() > kMaxPoints || totalPoints > kMaxTotalPoints) {
                    r.fail(QStringLiteral("a pen stroke needs 1 to %1 points").arg(kMaxPoints));
                    return give(false);
                }
                QPolygonF polygon;
                for (const QJsonValue &p : points) {
                    QPointF point;
                    if (!r.point(p, &point, "point")) return give(false);
                    polygon << point;
                }
                auto *pen = new PenPathItem(polygon.first());
                for (qsizetype i = 1; i < polygon.size(); ++i) pen->addPoint(polygon[i]);
                a = pen;
            } else if (type == QLatin1String("rect")) {
                a = new RectItem(rect);
            } else if (type == QLatin1String("ellipse")) {
                a = new EllipseItem(rect);
            } else if (type == QLatin1String("highlight")) {
                a = new HighlightItem(rect);
            } else if (type == QLatin1String("redact")) {
                RedactMode mode = RedactMode::Blur;
                bool known = false;
                for (const auto &[name, m] : kRedactModes)
                    if (o.value("mode").toString() == QLatin1String(name)) { mode = m; known = true; }
                if (!known) { r.fail(QStringLiteral("unknown redaction mode")); return give(false); }
                a = new RedactItem(mode, background, rect);
            } else if (type == QLatin1String("spotlight")) {
                double intensity = 0;
                if (!r.number(o.value("intensity"), 1, 3, &intensity, "intensity")) return give(false);
                auto *spot = new SpotlightItem(rect, QSizeF(canvas));
                spot->setSpotlightShape(o.value("shape").toString() == QLatin1String("ellipse")
                                            ? SpotlightShape::Ellipse : SpotlightShape::RoundedRect);
                spot->setIntensity(int(intensity));
                a = spot;
            } else {
                r.fail(QStringLiteral("unknown annotation type %1").arg(type));
                return give(false);
            }
            a->setStrokeColor(color);
            a->setStrokeWidth(width);
            item = a;
        }
        item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        item->setZValue(z);
        item->setPos(pos);
        // A move marks OCR results stale, so they come back after the position.
        if (auto *redact = dynamic_cast<RedactItem *>(item)) {
            QVector<QRectF> rects;
            for (const QJsonValue &v : o.value("textRects").toArray()) {
                QRectF rect;
                if (!r.rect(v, &rect, "textRects")) { delete item; return give(false); }
                rects.append(rect);
            }
            redact->setTextRects(rects);
            redact->setDetecting(o.value("detecting").toBool(true));
        }
        made.append(item);
    }
    return give(true);
}

namespace {
const std::pair<const char *, ExportSettings::Format> kFormats[] = {
    {"original", ExportSettings::Format::Original}, {"mp4", ExportSettings::Format::Mp4},
    {"webm", ExportSettings::Format::WebM}, {"gif", ExportSettings::Format::Gif}};
}

QJsonObject projectToJson(const ProjectSnapshot &p) {
    QString format = QStringLiteral("original");
    for (const auto &[name, f] : kFormats)
        if (f == p.exportSettings.format) format = QLatin1String(name);
    QJsonObject json{
        {"format", "eddy.project"},
        {"version", kProjectFormatVersion},
        {"kind", p.kind == MediaKind::Video ? "video" : "image"},
        {"source", QJsonObject{{"asset", p.asset}, {"name", p.sourceName}, {"sha256", p.sha256},
                               {"size", p.assetSize}}},
        {"document", QJsonObject{{"size", QJsonArray{p.size.width(), p.size.height()}},
                                 {"duration", p.durationMs}}},
        {"crop", p.crop.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(rectJson(p.crop))},
        {"trim", QJsonObject{{"in", p.trimInMs}, {"out", p.trimOutMs}}},
        {"position", p.positionMs},
        {"studio", studioToJson(p.studio)},
        {"export", QJsonObject{{"format", format}, {"shortSide", p.exportSettings.shortSide},
                               {"fps", p.exportSettings.fps}}},
        {"items", p.items}};
    return json;
}

std::optional<ProjectSnapshot> projectFromJson(const QJsonObject &json, QString *error) {
    Reader r;
    ProjectSnapshot p;
    auto done = [&](bool ok) -> std::optional<ProjectSnapshot> {
        if (error) *error = ok ? QString() : r.error;
        if (!ok) return std::nullopt;
        return p;
    };
    if (json.value("format").toString() != QLatin1String("eddy.project"))
        return r.fail(QStringLiteral("not an Eddy project")), done(false);
    if (json.value("version").toDouble() != kProjectFormatVersion)
        return r.fail(QStringLiteral("unsupported project version %1").arg(json.value("version").toDouble())), done(false);
    p.kind = json.value("kind").toString() == QLatin1String("video") ? MediaKind::Video : MediaKind::Image;
    const QJsonObject source = json.value("source").toObject();
    p.asset = source.value("asset").toString();
    // Only a plain file name inside the assets folder; no paths, no traversal.
    if (p.asset.isEmpty() || p.asset.contains(QLatin1Char('/')) || p.asset.contains(QLatin1Char('\\'))
        || p.asset.startsWith(QLatin1Char('.')))
        return r.fail(QStringLiteral("the source asset must be a plain file name")), done(false);
    p.sourceName = source.value("name").toString();
    p.sha256 = source.value("sha256").toString();
    if (p.sha256.size() != 64) return r.fail(QStringLiteral("the source digest is missing")), done(false);
    double size = 0;
    if (!r.number(source.value("size"), 0, 1e15, &size, "source size")) return done(false);
    p.assetSize = qint64(size);
    const QJsonObject document = json.value("document").toObject();
    const QJsonArray dims = document.value("size").toArray();
    double w = 0, h = 0, duration = 0;
    if (dims.size() != 2 || !r.number(dims[0], 1, 100000, &w, "width") || !r.number(dims[1], 1, 100000, &h, "height")
        || !r.number(document.value("duration"), 0, 1e12, &duration, "duration"))
        return done(false);
    p.size = QSize(int(w), int(h));
    p.durationMs = qint64(duration);
    if (!json.value("crop").isNull()) {
        QRectF crop;
        if (!r.rect(json.value("crop"), &crop, "crop")) return done(false);
        p.crop = crop.toRect();
        if (!QRect(QPoint(), p.size).contains(p.crop)) return r.fail(QStringLiteral("crop leaves the document")), done(false);
    }
    const QJsonObject trim = json.value("trim").toObject();
    double in = 0, out = 0, position = 0;
    if (!r.number(trim.value("in"), 0, 1e12, &in, "trim in") || !r.number(trim.value("out"), -1, 1e12, &out, "trim out")
        || !r.number(json.value("position"), 0, 1e12, &position, "position"))
        return done(false);
    p.trimInMs = qint64(in);
    p.trimOutMs = qint64(out);
    p.positionMs = qint64(position);
    QString studioError;
    const auto studio = studioFromJson(json.value("studio").toObject(), p.durationMs, p.size, &studioError);
    if (!studio) return r.fail(QStringLiteral("Studio: ") + studioError), done(false);
    p.studio = *studio;
    const QJsonObject exportJson = json.value("export").toObject();
    for (const auto &[name, f] : kFormats)
        if (exportJson.value("format").toString() == QLatin1String(name)) p.exportSettings.format = f;
    const int side = exportJson.value("shortSide").toInt();
    p.exportSettings.shortSide = side == 1080 || side == 720 || side == 480 ? side : 0;
    const int fps = exportJson.value("fps").toInt(60);
    p.exportSettings.fps = fps == 30 || fps == 15 ? fps : 60;
    if (!json.value("items").isArray()) return r.fail(QStringLiteral("items are missing")), done(false);
    p.items = json.value("items").toArray();
    return done(true);
}

}

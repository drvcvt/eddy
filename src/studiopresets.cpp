#include "studiopresets.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace eddy {

static constexpr qint64 kMaxImageBytes = 8 * 1024 * 1024;
static constexpr int kMaxImageSide = 3840;
static constexpr qint64 kMaxPresetBytes = 16 * 1024 * 1024;

QString studioPresetsDir(const QString &configPath) {
    return QFileInfo(configPath).absoluteDir().filePath(QStringLiteral("studio-presets"));
}

QString studioBackgroundsDir() {
    const QByteArray env = qgetenv("EDDY_DATA_DIR");
    const QString root = env.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("eddy"))
        : QString::fromLocal8Bit(env);
    return QDir(root).filePath(QStringLiteral("backgrounds"));
}

// The style and motion as the Studio block writes them.
static QJsonObject presetJson(const StudioPreset &preset) {
    StudioDocument doc;
    doc.style = preset.style;
    doc.motion = preset.motion;
    const QJsonObject studio = studioToJson(doc);
    return QJsonObject{{"format", "eddy.studio-preset"}, {"version", kStudioPresetVersion},
                       {"name", preset.name}, {"style", studio.value("style")}, {"camera", studio.value("camera")}};
}

static std::optional<StudioPreset> presetFrom(const QJsonObject &json, QString *error) {
    auto fail = [&](const QString &why) { if (error) *error = why; return std::optional<StudioPreset>(); };
    if (json.value("format").toString() != QLatin1String("eddy.studio-preset")) return fail(QStringLiteral("not a Studio preset"));
    if (json.value("version").toDouble() != kStudioPresetVersion)
        return fail(QStringLiteral("unsupported preset version %1").arg(json.value("version").toDouble()));
    const QString name = json.value("name").toString().trimmed();
    if (name.isEmpty() || name.size() > 80) return fail(QStringLiteral("a preset needs a name up to 80 characters"));
    QString studioError;
    const auto doc = studioFromJson(QJsonObject{{"version", kStudioFormatVersion}, {"style", json.value("style")},
                                                {"camera", json.value("camera")}}, 0, QSize(1, 1), &studioError);
    if (!doc) return fail(studioError);
    return StudioPreset{name, doc->style, doc->motion};
}

// Readable where it can be; a name that had to change gets a short hash of
// itself, so two names never share one file.
static QString fileNameFor(const QString &name) {
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N} _.-]")), QStringLiteral("_"));
    safe = safe.trimmed();
    if (safe.isEmpty() || safe.startsWith(QLatin1Char('.'))) safe.prepend(QStringLiteral("preset"));
    if (safe != name)
        safe += QLatin1Char('-') + QString::fromLatin1(
            QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex().left(8));
    return safe + QStringLiteral(".json");
}

QVector<StudioPreset> loadStudioPresets(const QString &configPath) {
    QVector<StudioPreset> out;
    for (const QFileInfo &file : QDir(studioPresetsDir(configPath)).entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        if (file.size() > kMaxPresetBytes) continue;
        QFile f(file.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        if (const auto preset = presetFrom(QJsonDocument::fromJson(f.readAll()).object(), nullptr)) out.append(*preset);
    }
    std::sort(out.begin(), out.end(), [](const StudioPreset &a, const StudioPreset &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}

DeliverResult saveStudioPreset(const QString &configPath, const StudioPreset &preset) {
    DeliverResult r;
    const QString dir = studioPresetsDir(configPath);
    if (!QDir().mkpath(dir)) {
        r.error = QStringLiteral("cannot create %1").arg(dir);
        return r;
    }
    QSaveFile file(QDir(dir).filePath(fileNameFor(preset.name)));
    file.setDirectWriteFallback(false);
    const QByteArray json = QJsonDocument(presetJson(preset)).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        r.error = QStringLiteral("cannot save the preset: %1").arg(file.errorString());
        return r;
    }
    r.ok = true;
    r.path = file.fileName();
    return r;
}

QByteArray exportStudioPreset(const StudioPreset &preset, QString *error) {
    QJsonObject json = presetJson(preset);
    if (preset.style.background == StudioStyle::Background::Image) {
        QFile file(preset.style.imagePath);
        QByteArray bytes;
        if (file.open(QIODevice::ReadOnly)) bytes = file.readAll();
        // The type comes from the bytes; anything but PNG and JPEG is re-encoded below.
        QBuffer probe(&bytes);
        QString type = QString::fromLatin1(QImageReader::imageFormat(&probe));
        QImage image = QImage::fromData(bytes);
        if (image.isNull()) {
            if (error) *error = QStringLiteral("cannot read the background image");
            return {};
        }
        // Too large: shrink and store as JPEG.
        if (bytes.size() > kMaxImageBytes || image.width() > kMaxImageSide || image.height() > kMaxImageSide
            || (type != QLatin1String("png") && type != QLatin1String("jpeg"))) {
            if (image.width() > kMaxImageSide || image.height() > kMaxImageSide)
                image = image.scaled(kMaxImageSide, kMaxImageSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            bytes.clear();
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            image.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", 90);
            type = QStringLiteral("jpeg");
        }
        if (bytes.size() > kMaxImageBytes) {
            if (error) *error = QStringLiteral("the background image is larger than 8 MB");
            return {};
        }
        QJsonObject style = json.value("style").toObject();
        style["image"] = QJsonObject{{"type", type}, {"data", QString::fromLatin1(bytes.toBase64())}};
        json["style"] = style;
    }
    return QJsonDocument(json).toJson();
}

std::optional<StudioPreset> importStudioPreset(const QByteArray &data, QString *error) {
    auto fail = [&](const QString &why) { if (error) *error = why; return std::optional<StudioPreset>(); };
    if (data.size() > kMaxPresetBytes) return fail(QStringLiteral("the preset file is too large"));
    QJsonParseError parse;
    QJsonObject json = QJsonDocument::fromJson(data, &parse).object();
    if (parse.error != QJsonParseError::NoError) return fail(QStringLiteral("not valid JSON: %1").arg(parse.errorString()));
    QJsonObject style = json.value("style").toObject();
    QString imagePath;
    if (style.value("image").isObject()) {
        const QJsonObject image = style.value("image").toObject();
        const QString type = image.value("type").toString();
        const QByteArray bytes = QByteArray::fromBase64(image.value("data").toString().toLatin1());
        const QImage decoded = QImage::fromData(bytes);
        if ((type != QLatin1String("png") && type != QLatin1String("jpeg")) || bytes.size() > kMaxImageBytes
            || decoded.isNull() || decoded.width() > kMaxImageSide || decoded.height() > kMaxImageSide)
            return fail(QStringLiteral("the embedded background is not a PNG or JPEG up to 3840 px and 8 MB"));
        // Stored once per content, so importing twice keeps one file.
        const QString dir = studioBackgroundsDir();
        QDir().mkpath(dir);
        imagePath = QDir(dir).filePath(QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
                                       + (type == QLatin1String("png") ? QStringLiteral(".png") : QStringLiteral(".jpg")));
        if (!QFileInfo::exists(imagePath)) {
            QSaveFile file(imagePath);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
                return fail(QStringLiteral("cannot store the background image"));
        }
        style["image"] = imagePath;
        json["style"] = style;
    }
    return presetFrom(json, error);
}

}

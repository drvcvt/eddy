#include "exportsettings.h"
#include <QFileInfo>
#include <QSettings>
#include <algorithm>

namespace eddy {

ExportSettings exportPreset(ExportPreset preset) {
    using F = ExportSettings::Format;
    switch (preset) {
    case ExportPreset::Original: return {};
    case ExportPreset::Web: return {F::Mp4, 1080, 60};
    case ExportPreset::Small: return {F::Mp4, 720, 30};
    case ExportPreset::Gif: return {F::Gif, 480, 15};
    }
    return {};
}

std::optional<ExportPreset> presetOf(const ExportSettings &settings) {
    for (auto preset : {ExportPreset::Original, ExportPreset::Web, ExportPreset::Small, ExportPreset::Gif})
        if (exportPreset(preset) == settings) return preset;
    return std::nullopt;
}

QString exportSuffix(const ExportSettings &settings, const QString &sourcePath) {
    switch (settings.format) {
    case ExportSettings::Format::Mp4: return QStringLiteral("mp4");
    case ExportSettings::Format::WebM: return QStringLiteral("webm");
    case ExportSettings::Format::Gif: return QStringLiteral("gif");
    case ExportSettings::Format::Original: break;
    }
    const QString suffix = QFileInfo(sourcePath).suffix().toLower();
    return suffix.isEmpty() ? QStringLiteral("mp4") : suffix;
}

QSize exportSize(QSize framed, int shortSide) {
    const int shorter = std::min(framed.width(), framed.height());
    if (shortSide <= 0 || shorter <= shortSide || shorter <= 0) return framed;
    const double scale = double(shortSide) / shorter;
    auto even = [](double v) { return std::max(2, qRound(v / 2) * 2); };
    return QSize(even(framed.width() * scale), even(framed.height() * scale));
}

ExportSettings loadExportSettings(const QString &configPath) {
    ExportSettings settings;
    if (configPath.isEmpty()) return settings;
    QSettings s(configPath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("export"));
    const QString format = s.value(QStringLiteral("format")).toString();
    using F = ExportSettings::Format;
    settings.format = format == QLatin1String("mp4") ? F::Mp4 : format == QLatin1String("webm") ? F::WebM
        : format == QLatin1String("gif") ? F::Gif : F::Original;
    const int side = s.value(QStringLiteral("short_side"), 0).toInt();
    settings.shortSide = side == 1080 || side == 720 || side == 480 ? side : 0;
    const int fps = s.value(QStringLiteral("fps"), 60).toInt();
    settings.fps = fps == 30 || fps == 15 ? fps : 60;
    return settings;
}

void saveExportSettings(const QString &configPath, const ExportSettings &settings) {
    if (configPath.isEmpty()) return;
    QSettings s(configPath, QSettings::IniFormat);
    s.beginGroup(QStringLiteral("export"));
    using F = ExportSettings::Format;
    s.setValue(QStringLiteral("format"), settings.format == F::Mp4 ? QStringLiteral("mp4")
        : settings.format == F::WebM ? QStringLiteral("webm")
        : settings.format == F::Gif ? QStringLiteral("gif") : QStringLiteral("original"));
    s.setValue(QStringLiteral("short_side"), settings.shortSide);
    s.setValue(QStringLiteral("fps"), settings.fps);
}

}

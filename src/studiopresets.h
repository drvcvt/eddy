#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>
#include "exporter.h"
#include "studiodocument.h"

namespace eddy {

// Shareable Studio presets (studio plan 6.10): a style and the camera's
// default motion, never time data. One JSON file per preset beside the
// config; an image background travels inside an exported preset.
struct StudioPreset {
    QString name;
    StudioStyle style;
    ZoomSegment::Motion motion = ZoomSegment::Motion::Focused;
    bool operator==(const StudioPreset &) const = default;
};

inline constexpr int kStudioPresetVersion = 1;

QString studioPresetsDir(const QString &configPath);
// EDDY_DATA_DIR, else the app data folder: where imported backgrounds live.
QString studioBackgroundsDir();

QVector<StudioPreset> loadStudioPresets(const QString &configPath);   // by name; bad files skipped
DeliverResult saveStudioPreset(const QString &configPath, const StudioPreset &preset);
// A self-contained file: the background image is embedded (at most 3840 px
// and 8 MB).
QByteArray exportStudioPreset(const StudioPreset &preset, QString *error);
// Unpacks an embedded background into studioBackgroundsDir().
std::optional<StudioPreset> importStudioPreset(const QByteArray &json, QString *error);

}

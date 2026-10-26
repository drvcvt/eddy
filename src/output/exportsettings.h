#pragma once
#include <QSize>
#include <QString>
#include <optional>

namespace eddy {

// How a video leaves Eddy (studio plan 6.9): the export popover at Save sets
// it for every route, and the config remembers the last choice.
struct ExportSettings {
    enum class Format { Original, Mp4, WebM, Gif };
    Format format = Format::Original;   // Original: the source's own container
    int shortSide = 0;                  // 0: full size; never scales up
    int fps = 60;                       // at most this many frames per second
    bool operator==(const ExportSettings &) const = default;
};

enum class ExportPreset { Original, Web, Small, Gif };

ExportSettings exportPreset(ExportPreset preset);
// The preset these settings match, if any.
std::optional<ExportPreset> presetOf(const ExportSettings &settings);
// The output file's suffix: the format's, or the source's for Original.
QString exportSuffix(const ExportSettings &settings, const QString &sourcePath);
// `framed` shrunk so its shorter side is at most `shortSide`, in even pixels.
QSize exportSize(QSize framed, int shortSide);
ExportSettings loadExportSettings(const QString &configPath);
void saveExportSettings(const QString &configPath, const ExportSettings &settings);

}

#pragma once
#include <QColor>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

namespace eddy {

// Studio framing: the media sits on a background with padding, rounded
// corners and a drop shadow (docs/specs/2026-09-23-studio-mode.md). Lengths
// are percent of the content's shorter side, so a style looks the same on a
// small crop and on a 4K screenshot.
struct StudioStyle {
    enum class Background { None, Color, Gradient, Image };
    Background background = Background::None;
    QColor color = QColor(0x1f, 0x1f, 0x1f);
    QColor color2 = QColor(0x3a, 0x3a, 0x3a);   // gradient end
    int gradientAngle = 135;                    // degrees, 0 = left to right
    QString imagePath;
    double padding = 8.0;
    double radius = 2.0;
    double shadow = 50.0;                       // strength 0..100
    QSize aspect;                               // empty: follow the content

    bool active() const { return background != Background::None; }
    bool operator==(const StudioStyle &) const = default;
};

// Curated backgrounds offered in the Studio popover.
struct StudioBackgroundPreset {
    QString name;
    StudioStyle::Background kind;
    QColor color, color2;
};
QList<StudioBackgroundPreset> studioBackgroundPresets();

struct StudioLayout {
    QSize output;
    QRect content;       // where the media lands inside the output
    double radius = 0;   // content corner radius in output pixels
};

StudioLayout studioLayout(QSize content, const StudioStyle &style);
// The background with the content's shadow already cast; the content is drawn
// over it through renderStudioMask.
QImage renderStudioBackground(QSize content, const StudioStyle &style);
// The rounded-corner mask for the content: grayscale, white inside, black
// outside, antialiased edges.
QImage renderStudioMask(QSize content, const StudioStyle &style);
// Output-sized grayscale coverage of the background: white where it shows,
// black over the content, antialiased at the rounded corners. Drives ffmpeg's
// maskedmerge between the padded video and the background still.
QImage renderStudioFrameMask(QSize content, const StudioStyle &style);
// The last style used, offered when Studio is switched on again. Stored in
// the config file's [studio] group; the default is the Dusk gradient.
StudioStyle loadLastStudioStyle(const QString &configPath);
void saveLastStudioStyle(const QString &configPath, const StudioStyle &style);

// Complete framed image; returns `content` unchanged when the style is off.
QImage renderStudioImage(const QImage &content, const StudioStyle &style);

}

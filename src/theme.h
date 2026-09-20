#pragma once
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QSize>
#include <QString>

namespace eddy {

enum class ThemeMode { System, Dark, Light };

namespace theme {

// Vis grayscale tokens. Annotation colours remain independent.
inline constexpr const char *kAccent          = "#414141";
inline constexpr const char *kBar             = "#202020";
inline constexpr const char *kCanvas          = "#181818";
inline constexpr const char *kIconRest        = "#999999";
inline constexpr const char *kIconActive      = "#EEEEEE";
inline constexpr const char *kStroke          = "#ff3b30";

// A fully dark palette so native widgets (colour dialog, text caret/selection,
// tooltips, scrollbars) stay dark regardless of the host GTK/Qt theme.
QPalette darkPalette();
QPalette palette(bool dark);
bool resolveDark(ThemeMode mode, const QPalette &systemPalette);
QString styleSheet(bool dark);

// Two control sizes carry the whole app: dense chrome (top bar, tool rail,
// playback row) and the floating bars that sit over the canvas.
inline constexpr QSize kBarButton{22, 22};
inline constexpr QSize kFloatButton{24, 24};
inline constexpr int kIconSize = 18;
// The floating bars sit right on the canvas with nothing else competing, so
// their glyphs run larger and nearly fill the button.
inline constexpr int kFloatIcon = 20;

// Render an SVG (resource path) to a monochrome QIcon: `rest` colour for the
// Off/Normal state, `active` for the On state. Rendered at 2x `size`, so pass
// the size the button actually shows or the pixmap gets resampled.
QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active,
                 int size = kIconSize);

} // namespace theme
} // namespace eddy

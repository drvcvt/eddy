#pragma once
#include <QColor>
#include <QPalette>
#include <QIcon>
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

// Render an SVG (resource path) to a monochrome QIcon: `rest` colour for the
// Off/Normal state, `active` for the On state. HiDPI-crisp.
QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active);

} // namespace theme
} // namespace eddy

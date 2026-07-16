#pragma once
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QString>

namespace eddy {

enum class ThemeMode { System, Dark, Light };

namespace theme {

// Approved mt-ui-style dark tokens. Annotation colours remain independent.
inline constexpr const char *kAccent          = "#ECECEC";
inline constexpr const char *kBar             = "#1A1A1A";
inline constexpr const char *kCanvas          = "#121212";
inline constexpr const char *kIconRest        = "#969696";
inline constexpr const char *kIconActive      = "#121212";
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

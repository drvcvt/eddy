#pragma once
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QString>

namespace eddy::theme {

// Accent mirrors the user's quickshell accent (mono override) — deliberately
// monochrome, not blue. The active tool glyph sits on the light pill, so it is
// dark (a cutout).
inline constexpr const char *kAccent          = "#cccccc";
inline constexpr const char *kAccentSecondary = "#7a7a7a";
inline constexpr const char *kAccentMuted     = "#232323";
inline constexpr const char *kBar             = "#15161c";
inline constexpr const char *kCanvas          = "#0e0f13";
inline constexpr const char *kIconRest        = "#b7bac6";
inline constexpr const char *kIconActive      = "#15161c";
inline constexpr const char *kStroke          = "#ff3b30";

// A fully dark palette so native widgets (colour dialog, text caret/selection,
// tooltips, scrollbars) stay dark regardless of the host GTK/Qt theme.
QPalette darkPalette();

// Render an SVG (resource path) to a monochrome QIcon: `rest` colour for the
// Off/Normal state, `active` for the On state. HiDPI-crisp.
QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active);

} // namespace eddy::theme

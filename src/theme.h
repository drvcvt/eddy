#pragma once
#include <QColor>
#include <QPalette>
#include <QIcon>
#include <QSize>
#include <QString>

class QToolButton;

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
// The type scale: meta/labels and everything else. Painted text uses these too.
inline constexpr int kFsMicro = 11;
inline constexpr int kFsSmall = 13;
inline constexpr int kFsBody = 15;    // a label beside a full-size chrome icon
inline constexpr int kIconSize = 18;
// tintedIcon insets the glyph by size/11 per side; this is the ink that remains.
constexpr int iconInk(int size) { return size * 9 / 11; }
// The floating bars sit right on the canvas with nothing else competing, so
// their glyphs run larger and nearly fill the button.
inline constexpr int kFloatIcon = 20;

// An SVG (resource path) as a monochrome QIcon: `rest` colour for the Off/Normal
// state, `active` for the On state. It rasterises per paint at the real device
// pixel size, so `size` only documents what the caller displays.
QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active,
                 int size = kIconSize);

// Menu buttons carry our rounded, filled arrow after the label, at the label's size,
// instead of the platform's sharp indicator hanging off the baseline.
void setMenuArrow(QToolButton *button);
// Qt pads every tool button label by two spaces; beside an icon that slack
// lands between label and chevron, so menu buttons are sized without it.
void setMenuLabel(QToolButton *button, const QString &text);

// Tooltips put a shortcut after a tab ("Save\tEnter") and further hints on
// their own lines. The compact tooltip shows the shortcut as a quieter
// right-hand column instead of a separator glyph.
QString tooltipHtml(const QString &tip);

} // namespace theme
} // namespace eddy

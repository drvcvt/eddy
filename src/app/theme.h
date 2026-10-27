#pragma once
#include <QColor>
#include <QPalette>
#include <QPainterPath>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QToolButton>

class QWidget;

class QMenu;
class QPainter;

namespace eddy {

enum class ThemeMode { System, Dark, Light };

namespace theme {

// Neutral graphite from one OKLCH hue; only lightness changes (docs/design.md).
// Annotation colours are content and stay independent.
// Bg is the frame (bars, tool rail); Well the workspace behind the picture, a touch deeper.
enum class Tone { Canvas, Bg, Raise1, Raise2, Raise3, SegmentOn, Faint, Sub, Fg, BarHover, BarActive, Well };
QColor tone(Tone tone, bool dark);
inline constexpr const char *kStroke          = "#ff3b30";

// A fully dark palette so native widgets (colour dialog, text caret/selection,
// tooltips, scrollbars) stay dark regardless of the host GTK/Qt theme.
QPalette darkPalette();
QPalette palette(bool dark);
bool resolveDark(ThemeMode mode, const QPalette &systemPalette);
QString styleSheet(bool dark);

// Geometry on a 4 px grid (docs/design.md). Every text and icon control is kCtl
// high; bars are kCtl plus kBarPad on each side.
inline constexpr int kS1 = 4, kS2 = 8, kS3 = 12, kS4 = 16, kS6 = 24;
inline constexpr int kCtl = 26;                      // 2x kFsUi
inline constexpr QSize kControl{kCtl, kCtl};
inline constexpr int kBarPad = 5;                    // (kBar - kCtl) / 2, the one value off the grid
inline constexpr int kBar = kCtl + 2 * kBarPad;      // top bar, footer, tool rail width
inline constexpr int kRChip = 6;                     // a control's fill
inline constexpr int kRBar = kRChip + kS1;           // a floating bar or menu around kS1-inset chips
inline constexpr int kRPop = 14;                     // popovers, toast; children take kRChip
// The selection and hover mark of an icon button in the top bar and tool rail: a
// squircle centred in the kCtl button, kMarkInset in from its edges.
inline constexpr int kMarkInset = 2;   // a kCtl - 4 mark: inside the button, never touching its neighbour's
// The type scale: captions and technical values, then labels. Painted text uses these too.
inline constexpr int kFsCaption = 12;
inline constexpr int kFsUi = 13;
inline constexpr int kFsDisplay = 18;
inline constexpr int kIconSize = 18;                 // a free-standing chrome icon
inline constexpr int kLabelIcon = 16;                // beside a kFsUi label: its ink (13 px) no taller than the text
// tintedIcon insets the glyph by size/11 per side; this is the ink that remains.
constexpr int iconInk(int size) { return size * 9 / 11; }
// The floating bars sit right on the canvas with nothing else competing, so
// their glyphs run larger and nearly fill the button.
inline constexpr int kFloatIcon = 20;

// An SVG (resource path) as a monochrome QIcon: `rest` colour for the Off/Normal
// state, `active` for the On state. It rasterises per paint at the real device
// pixel size, so `size` only documents what the caller displays.
// A superellipse filling `rect`: corners that flow into the sides, where a rounded
// rectangle joins an arc to a straight edge.
QPainterPath squircle(const QRectF &rect);
// A rectangle whose corners are quarter superellipses `radius` across: the
// curvature grows into the corner instead of an arc meeting a straight edge.
// Only the corners in `rounded` curve; the rest stay square.
enum Corner { TopLeft = 1, TopRight = 2, BottomLeft = 4, BottomRight = 8, AllCorners = 15 };
QPainterPath squircleRect(const QRectF &rect, qreal radius, int rounded = AllCorners);
// Where a popover opens from a top-bar button: `s1` under the bar, on the button's
// side (its left edge, or its right edge with `alignRight`), inside the window.
QPoint popoverPosition(const QWidget *button, const QWidget *bar, QSize size, bool alignRight);
// A button's menu opens at popoverPosition instead of on the bar's edge.
void placeMenuAtBar(QMenu *menu, const QWidget *button, const QWidget *bar, bool alignRight);
// Menus open on rounded corners: frameless and translucent, so the stylesheet's
// radius shows instead of the window's square edge. Before the menu first shows.
void roundMenu(QMenu *menu);
// The one grip of the canvas (resize, crop): a 10 px squircle in the text colour on a
// 1 px ring of the window colour, so it holds on light and dark content alike.
void paintGrip(QPainter *painter, QPointF centre);
// The frame of a selected or edited text: the grip's two colours as a 1 px line.
void paintSelectionFrame(QPainter *painter, const QRectF &rect);

QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active,
                 int size = kIconSize);

// Menu buttons carry our rounded, filled arrow after the label, at the label's size,
// instead of the platform's sharp indicator hanging off the baseline.
void setMenuArrow(QToolButton *button);
// An icon button of the floating context bars: raised on hover, no focus.
QToolButton *floatButton(QWidget *parent, const QString &name, const QString &tip);
// Qt pads every tool button label by two spaces; beside an icon that slack
// lands between label and chevron, so menu buttons are sized without it.
void setMenuLabel(QToolButton *button, const QString &text);

// A tool button that draws its own label: the capitals centred on its height,
// the baseline on a whole device pixel. Qt centres the line box instead, which
// leaves the capitals up to a pixel low and cannot be fixed from the stylesheet.
// An icon sits left of the text, its ink centred on the capitals and kS2 away.
class CapButton : public QToolButton {
public:
    using QToolButton::QToolButton;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent *) override;
private:
    // The glyph's ink inside its pixmap, in logical pixels; measured, not
    // derived from the SVG, so every glyph sits by what is actually drawn.
    QRectF inkRect(qreal dpr) const;
    qreal contentWidth() const;
    mutable QRectF m_ink;
    mutable qreal m_inkDpr = 0;
};

// Tooltips put a shortcut after a tab ("Save\tEnter") and further hints on
// their own lines. The compact tooltip shows the shortcut as a quieter
// right-hand column instead of a separator glyph.
QString tooltipHtml(const QString &tip);
// What a screen reader should say for a tooltip: its first line, without the
// shortcut after the tab ("Undo\tCtrl+Z" reads "Undo").
QString spokenName(const QString &tip);

} // namespace theme
} // namespace eddy

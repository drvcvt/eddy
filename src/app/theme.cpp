#include "app/theme.h"
#include <QDebug>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QFile>
#include <QIconEngine>
#include <QFontDatabase>
#include <QFontInfo>
#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QtMath>
#include <algorithm>
#include <cmath>

// Force the AUTORCC static-library resources to register at start-up.
// Without this call a static lib's qrc initialiser can be discarded by the
// linker when no other symbol from qrc_eddy.cpp.o is directly referenced.
static void initResources() { Q_INIT_RESOURCE(eddy); }
static const bool kResourcesInited = (initResources(), true);

namespace eddy::theme {

namespace {
// OKLCH to sRGB (Björn Ottosson's OKLab matrices), clamped to the gamut.
QColor oklch(qreal l, qreal c, qreal hueDegrees) {
    const qreal h = qDegreesToRadians(hueDegrees);
    const qreal a = c * std::cos(h), b = c * std::sin(h);
    const qreal l_ = l + 0.3963377774 * a + 0.2158037573 * b;
    const qreal m_ = l - 0.1055613458 * a - 0.0638541728 * b;
    const qreal s_ = l - 0.0894841775 * a - 1.2914855480 * b;
    const qreal L = l_ * l_ * l_, M = m_ * m_ * m_, S = s_ * s_ * s_;
    auto encode = [](qreal x) {
        x = std::clamp<qreal>(x, 0, 1);
        return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1 / 2.4) - 0.055;
    };
    return QColor::fromRgbF(encode(4.0767416621 * L - 3.3077115913 * M + 0.2309699292 * S),
                            encode(-1.2684380046 * L + 2.6097574011 * M - 0.3413193965 * S),
                            encode(-0.0041960863 * L - 0.7034186147 * M + 1.7076147010 * S));
}
} // namespace

// Plain grey: one lightness ladder in OKLab, no hue (mt, 2026-10-27: a blue tint read as blue).
QColor tone(Tone which, bool dark) {
    constexpr qreal kHue = 0, kChroma = 0;
    struct Step { qreal dark, light; };
    static constexpr Step kSteps[] = {
        {0.13, 0.93},    // Canvas
        {0.205, 0.985},  // Bg
        {0.235, 0.96},   // Raise1
        {0.295, 0.935},  // Raise2
        {0.335, 0.91},   // Raise3
        {0.37, 1.0},     // SegmentOn
        {0.50, 0.72},    // Faint
        {0.72, 0.50},    // Sub
        {0.95, 0.22},    // Fg
        {0.235, 0.94},   // BarHover
        {0.28, 0.915},   // BarActive
        {0.175, 0.955},  // Well
    };
    const Step step = kSteps[int(which)];
    return oklch(dark ? step.dark : step.light, kChroma, kHue);
}

QPalette darkPalette() {
    return palette(true);
}

QPalette palette(bool dark) {
    QPalette p;
    const QColor bg = tone(Tone::Bg, dark), base = tone(Tone::Raise1, dark), text = tone(Tone::Fg, dark);
    const QColor disabled = tone(Tone::Faint, dark);
    const QColor highlight = dark ? tone(Tone::Raise3, dark) : text;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, bg);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, base);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, dark ? text : bg);
    p.setColor(QPalette::PlaceholderText, tone(Tone::Sub, dark));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return p;
}

bool resolveDark(ThemeMode mode, const QPalette &systemPalette) {
    if (mode == ThemeMode::Dark) return true;
    if (mode == ThemeMode::Light) return false;
    return systemPalette.color(QPalette::Window).lightness() < 128;
}

QString styleSheet(bool dark) {
    QFile file(QStringLiteral(":/eddy.qss"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QString qss = QString::fromUtf8(file.readAll());
    // Outfit ships in the binary so every platform gets the same UI face.
    static const int outfit = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/Outfit.ttf"));
    Q_UNUSED(outfit);
    // One type scale for the whole app; the QSS never names a family or a px size.
    qss.replace("@font-ui", "\"Outfit\", sans-serif");
    // Mono is the user's own fixed font, as the desktop (qt6ct, GNOME, KDE, Windows) reports it.
    qss.replace("@font-mono", QStringLiteral("\"%1\", monospace")
        .arg(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
    const auto px = [](int v) { return QString::number(v) + QStringLiteral("px"); };
    // Longest names first: @fs-ui must not eat the start of a longer token.
    const QList<QPair<QString, QString>> sizes{
        {"@fs-caption", px(kFsCaption)}, {"@fs-display", px(kFsDisplay)}, {"@fs-ui", px(kFsUi)},
        {"@r-chip", px(kRChip)}, {"@r-bar", px(kRBar)}, {"@r-pop", px(kRPop)},
        {"@ctl", px(kCtl)}, {"@s1", px(kS1)}, {"@s2", px(kS2)}, {"@s3", px(kS3)}, {"@s4", px(kS4)}};
    for (const auto &[token, value] : sizes) qss.replace(token, value);
    const auto hex = [dark](Tone which) { return tone(which, dark).name(); };
    const QList<QPair<QString, QString>> colors{
        {"@bar-active", hex(Tone::BarActive)}, {"@bar-hover", hex(Tone::BarHover)},
        {"@bar", hex(Tone::Bg)},   // the frame: bars and rail on the window's own grey
        {"@chip-on-fg", dark ? hex(Tone::Fg) : hex(Tone::Bg)}, {"@chip-on", dark ? hex(Tone::Raise3) : hex(Tone::Fg)},
        {"@raise3", hex(Tone::Raise3)}, {"@raise2", hex(Tone::Raise2)}, {"@segment-on", hex(Tone::SegmentOn)},
        {"@raise1", hex(Tone::Raise1)}, {"@faint", hex(Tone::Faint)},
        {"@sub", hex(Tone::Sub)}, {"@fg", hex(Tone::Fg)}, {"@bg", hex(Tone::Bg)}};
    for (const auto &[token, color] : colors) qss.replace(token, color);
    return qss;
}

namespace {
// Rasterises the SVG at the exact device size of each paint. A pre-rendered 2x
// pixmap gets smooth-scaled on 1x and fractional displays, which is what made
// the icons softer than the text beside them.
class TintedIconEngine : public QIconEngine {
public:
    TintedIconEngine(const QString &path, const QColor &rest, const QColor &active)
        : m_path(path), m_rest(rest), m_active(active),
          m_hover(QApplication::palette().color(QPalette::WindowText)),
          m_disabled(QApplication::palette().color(QPalette::Disabled, QPalette::ButtonText)) {}
    QIconEngine *clone() const override { return new TintedIconEngine(*this); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        const QSize device = (QSizeF(size) * scale).toSize();
        QPixmap pm(device);
        pm.fill(Qt::transparent);
        QSvgRenderer renderer(m_path);
        // Room for the round caps, snapped to whole device pixels so the glyph
        // box never starts between two of them.
        const int inset = qRound(device.width() / 11.0);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p, QRectF(pm.rect()).adjusted(inset, inset, -inset, -inset));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), state == QIcon::On ? m_active
                              : mode == QIcon::Active ? m_hover
                              : mode == QIcon::Disabled ? m_disabled : m_rest);
        p.end();
        pm.setDevicePixelRatio(scale);
        return pm;
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override {
        const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, scale));
    }
private:
    QString m_path;
    QColor m_rest, m_active, m_hover, m_disabled;
};
} // namespace

QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active, int) {
    if (!QSvgRenderer(svgPath).isValid()) {
        qWarning("tintedIcon: invalid SVG '%s'", qPrintable(svgPath));
        return {};                              // invalid path -> null icon (caller-detectable)
    }
    return QIcon(new TintedIconEngine(svgPath, rest, active));
}

QToolButton *floatButton(QWidget *parent, const QString &name, const QString &tip) {
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(kControl);
    button->setIconSize(QSize(kFloatIcon, kFloatIcon));
    button->setToolTip(tip);
    button->setAccessibleName(spokenName(tip));
    return button;
}

void setMenuArrow(QToolButton *button) {
    button->ensurePolished();
    const int size = QFontInfo(button->font()).pixelSize();
    const QColor rest = QApplication::palette().color(QPalette::PlaceholderText);   // @sub, like the label
    button->setIcon(tintedIcon(QStringLiteral(":/icons/menu-arrow.svg"), rest, rest, size));
    button->setIconSize(QSize(size, size));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setLayoutDirection(Qt::RightToLeft);   // icon trails the label
    setMenuLabel(button, button->text());
}

void setMenuLabel(QToolButton *button, const QString &text) {
    button->setText(text);
    button->setMinimumWidth(0);
    button->setMaximumWidth(QWIDGETSIZE_MAX);
    button->setFixedWidth(button->sizeHint().width()
                          - 2 * button->fontMetrics().horizontalAdvance(QLatin1Char(' ')));
}

QRectF CapButton::inkRect(qreal dpr) const {
    if (icon().isNull()) return {};
    if (dpr != m_inkDpr) {
        const QImage image = icon().pixmap(iconSize(), dpr).toImage().convertToFormat(QImage::Format_ARGB32);
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) > 64) {
                    left = qMin(left, x); right = qMax(right, x);
                    top = qMin(top, y); bottom = qMax(bottom, y);
                }
        m_ink = right < 0 ? QRectF() : QRectF(left / dpr, top / dpr, (right - left + 1) / dpr,
                                              (bottom - top + 1) / dpr);
        m_inkDpr = dpr;
    }
    return m_ink;
}

qreal CapButton::contentWidth() const {
    const QRectF ink = inkRect(devicePixelRatioF());
    const qreal gap = text().isEmpty() ? 0 : kS2;
    return (ink.isNull() ? 0 : ink.width() + gap) + QFontMetricsF(font()).horizontalAdvance(text());
}

QSize CapButton::sizeHint() const {
    return QSize(qCeil(2 * kS2 + contentWidth()), kCtl);
}

void CapButton::paintEvent(QPaintEvent *) {
    QStylePainter p(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    option.text.clear();
    option.icon = QIcon();
    p.drawComplexControl(QStyle::CC_ToolButton, option);   // the stylesheet's fill, if any
    const qreal dpr = devicePixelRatioF();
    auto snap = [dpr](qreal v) { return std::round(v * dpr) / dpr; };
    const QFontMetricsF metrics(font());
    const qreal baseline = snap((height() + metrics.capHeight()) / 2);
    const qreal capMiddle = baseline - metrics.capHeight() / 2;
    // Centred as a whole when the button is wider than its content (a fixed width).
    qreal x = snap((width() - contentWidth()) / 2);
    const QRectF ink = inkRect(dpr);
    if (!ink.isNull()) {
        const QIcon::Mode mode = !isEnabled() ? QIcon::Disabled : underMouse() ? QIcon::Active : QIcon::Normal;
        p.drawPixmap(QPointF(snap(x - ink.left()), snap(capMiddle - ink.center().y())),
                     icon().pixmap(iconSize(), dpr, mode, isChecked() ? QIcon::On : QIcon::Off));
        x += ink.width() + kS2;
        if (text().isEmpty()) return;
    }
    p.setPen(palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
    p.drawText(QPointF(x, baseline), text());
}

QPainterPath squircle(const QRectF &rect) {
    // |x/a|^5 + |y/b|^5 = 1, sampled finely enough that no facet shows at this size.
    constexpr int kSteps = 96;
    constexpr qreal kExponent = 5;
    const QPointF centre = rect.center();
    const qreal a = rect.width() / 2, b = rect.height() / 2;
    QPolygonF outline;
    outline.reserve(kSteps);
    for (int i = 0; i < kSteps; ++i) {
        const qreal t = 2 * M_PI * i / kSteps, c = std::cos(t), s = std::sin(t);
        outline << QPointF(centre.x() + a * std::copysign(std::pow(std::abs(c), 2 / kExponent), c),
                           centre.y() + b * std::copysign(std::pow(std::abs(s), 2 / kExponent), s));
    }
    QPainterPath path;
    path.addPolygon(outline);
    path.closeSubpath();
    return path;
}

void paintGrip(QPainter *painter, QPointF centre) {
    const QPalette palette = QApplication::palette();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(palette.color(QPalette::Window), 1));
    painter->setBrush(palette.color(QPalette::WindowText));
    painter->drawPath(squircle(QRectF(centre - QPointF(5, 5), QSizeF(10, 10))));
    painter->restore();
}

void paintSelectionFrame(QPainter *painter, const QRectF &rect) {
    const QPalette palette = QApplication::palette();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setBrush(Qt::NoBrush);
    QPen ring(palette.color(QPalette::Window), 3);
    ring.setCosmetic(true);
    painter->setPen(ring);
    painter->drawRect(rect);
    QPen line(palette.color(QPalette::WindowText), 1);
    line.setCosmetic(true);
    painter->setPen(line);
    painter->drawRect(rect);
    painter->restore();
}

QPainterPath squircleRect(const QRectF &rect, qreal radius, int rounded) {
    constexpr int kSteps = 96;   // per full turn: 24 per corner
    // 4, not the marks' 5: a corner this large at 5 stays nearly square. At 4 a
    // 24 px corner reads like a 12 px arc, with curvature that eases in.
    constexpr qreal kExponent = 4;
    radius = std::min({radius, rect.width() / 2, rect.height() / 2});
    QPolygonF outline;
    outline.reserve(kSteps + 4);
    for (int i = 0; i <= kSteps; ++i) {
        const qreal t = 2 * M_PI * (i + 0.5) / kSteps, c = std::cos(t), s = std::sin(t);
        // Each quadrant curves about its own corner's centre; the jumps between
        // quadrants are the straight sides.
        const int corner = c >= 0 ? (s >= 0 ? BottomRight : TopRight) : (s >= 0 ? BottomLeft : TopLeft);
        if (!(rounded & corner)) {
            outline << QPointF(c >= 0 ? rect.right() : rect.left(), s >= 0 ? rect.bottom() : rect.top());
            continue;
        }
        const QPointF centre(c >= 0 ? rect.right() - radius : rect.left() + radius,
                             s >= 0 ? rect.bottom() - radius : rect.top() + radius);
        outline << QPointF(centre.x() + radius * std::copysign(std::pow(std::abs(c), 2 / kExponent), c),
                           centre.y() + radius * std::copysign(std::pow(std::abs(s), 2 / kExponent), s));
    }
    QPainterPath path;
    path.addPolygon(outline);
    path.closeSubpath();
    return path;
}

QString spokenName(const QString &tip) {
    return tip.section(QLatin1Char('\n'), 0, 0).section(QLatin1Char('\t'), 0, 0).trimmed();
}

QString tooltipHtml(const QString &tip) {
    const QString key = QApplication::palette().color(QPalette::PlaceholderText).name();
    QString rows;
    for (const QString &line : tip.split(QLatin1Char('\n'))) {
        const QString label = line.section(QLatin1Char('\t'), 0, 0).toHtmlEscaped();
        const QString shortcut = line.section(QLatin1Char('\t'), 1).toHtmlEscaped();
        rows += shortcut.isEmpty()
            ? QStringLiteral("<tr><td colspan=\"2\">%1</td></tr>").arg(label)
            : QStringLiteral("<tr><td>%1</td><td style=\"padding-left:12px; color:%2\" align=\"right\">%3</td></tr>")
                  .arg(label, key, shortcut);
    }
    return QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\">%1</table>").arg(rows);
}

} // namespace eddy::theme

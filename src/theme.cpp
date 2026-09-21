#include "theme.h"
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

// Force the AUTORCC static-library resources to register at start-up.
// Without this call a static lib's qrc initialiser can be discarded by the
// linker when no other symbol from qrc_eddy.cpp.o is directly referenced.
static void initResources() { Q_INIT_RESOURCE(eddy); }
static const bool kResourcesInited = (initResources(), true);

namespace eddy::theme {

QPalette darkPalette() {
    return palette(true);
}

QPalette palette(bool dark) {
    QPalette p;
    const QColor bg(dark ? "#181818" : "#FAFAFA");
    const QColor base(dark ? "#202020" : "#F1F1F1");
    const QColor text(dark ? "#EEEEEE" : "#1A1A1A");
    const QColor disabled(dark ? "#5C5C5C" : "#A6A6A6");
    const QColor highlight(dark ? "#414141" : "#1A1A1A");
    const QColor highlightedText(dark ? "#EEEEEE" : "#FAFAFA");
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
    p.setColor(QPalette::HighlightedText, highlightedText);
    p.setColor(QPalette::PlaceholderText, QColor(dark ? "#999999" : "#6E6E6E"));
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
    qss.replace("@fs-micro", QString::number(kFsMicro) + "px");
    qss.replace("@fs-small", QString::number(kFsSmall) + "px");
    qss.replace("@fs-body", QString::number(kFsBody) + "px");
    // The dark bar is translucent, so its state fills are pre-blended: stacking
    // two translucent layers smears the rounded corners into mush.
    const QList<QPair<QString, QString>> tokens = dark
        ? QList<QPair<QString, QString>>{
            {"@bar-active", "#2E2E2E"}, {"@bar-hover", "#212121"},
            {"@bar", "rgba(0, 0, 0, 153)"}, {"@chip-on-fg", "#EEEEEE"}, {"@chip-on", "#414141"},
            {"@raise3", "#414141"}, {"@raise2", "#353535"},
            {"@raise1", "#202020"}, {"@faint", "#5C5C5C"},
            {"@sub", "#999999"}, {"@fg", "#EEEEEE"}, {"@bg", "#181818"}}
        : QList<QPair<QString, QString>>{
            {"@bar-active", "rgba(0, 0, 0, 24)"}, {"@bar-hover", "rgba(0, 0, 0, 14)"},
            {"@bar", "#F1F1F1"}, {"@chip-on-fg", "#FAFAFA"}, {"@chip-on", "#1A1A1A"},
            {"@raise3", "#E0E0E0"}, {"@raise2", "#E9E9E9"},
            {"@raise1", "#F1F1F1"}, {"@faint", "#A6A6A6"},
            {"@sub", "#6E6E6E"}, {"@fg", "#1A1A1A"}, {"@bg", "#FAFAFA"}};
    for (const auto &[token, color] : tokens) qss.replace(token, color);
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

} // namespace eddy::theme

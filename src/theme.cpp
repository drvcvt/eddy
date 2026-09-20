#include "theme.h"
#include <QDebug>
#include <QApplication>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QFile>

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

QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active, int size) {
    auto render = [&](const QColor &c) {
        QSvgRenderer r(svgPath);
        if (!r.isValid()) {
            qWarning("tintedIcon: invalid SVG '%s'", qPrintable(svgPath));
            return QPixmap();
        }
        const int s = size * 2;                 // 2x logical, HiDPI-crisp
        const qreal inset = size / 11.0;        // room for the round caps at the edges
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p, QRectF(inset, inset, s - 2 * inset, s - 2 * inset));
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), c);
        p.end();
        pm.setDevicePixelRatio(2.0);
        return pm;
    };
    const QPixmap restPm = render(rest);
    if (restPm.isNull()) return {};             // invalid path -> null icon (caller-detectable)
    const QPixmap activePm = render(active);
    QIcon icon;
    icon.addPixmap(restPm,   QIcon::Normal,   QIcon::Off);
    icon.addPixmap(activePm, QIcon::Normal,   QIcon::On);
    icon.addPixmap(render(QApplication::palette().color(QPalette::WindowText)),
                   QIcon::Active, QIcon::Off);
    icon.addPixmap(activePm, QIcon::Active, QIcon::On);
    icon.addPixmap(render(QApplication::palette().color(QPalette::Disabled, QPalette::ButtonText)),
                   QIcon::Disabled, QIcon::Off);
    icon.addPixmap(activePm, QIcon::Selected, QIcon::On);
    return icon;
}

} // namespace eddy::theme

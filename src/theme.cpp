#include "theme.h"
#include <QDebug>
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
    const QColor bg(dark ? "#121212" : "#FAFAFA");
    const QColor base(dark ? "#1A1A1A" : "#F1F1F1");
    const QColor text(dark ? "#ECECEC" : "#1A1A1A");
    const QColor disabled(dark ? "#5C5C5C" : "#A0A0A0");
    const QColor highlight(dark ? "#ECECEC" : "#1A1A1A");
    const QColor highlightedText(dark ? "#1A1A1A" : "#FAFAFA");
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
    p.setColor(QPalette::PlaceholderText, disabled);
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
    const QList<QPair<QString, QString>> tokens = dark
        ? QList<QPair<QString, QString>>{
            {"@chip-on-fg", "#121212"}, {"@chip-on", "#ECECEC"},
            {"@raise3", "#2B2B2B"}, {"@raise2", "#222222"},
            {"@raise1", "#1A1A1A"}, {"@faint", "#5C5C5C"},
            {"@sub", "#969696"}, {"@fg", "#ECECEC"}, {"@bg", "#121212"}}
        : QList<QPair<QString, QString>>{
            {"@chip-on-fg", "#FAFAFA"}, {"@chip-on", "#1A1A1A"},
            {"@raise3", "#DEDEDE"}, {"@raise2", "#E8E8E8"},
            {"@raise1", "#F1F1F1"}, {"@faint", "#A0A0A0"},
            {"@sub", "#666666"}, {"@fg", "#1A1A1A"}, {"@bg", "#FAFAFA"}};
    for (const auto &[token, color] : tokens) qss.replace(token, color);
    return qss;
}

QIcon tintedIcon(const QString &svgPath, const QColor &rest, const QColor &active) {
    auto render = [&](const QColor &c) {
        QSvgRenderer r(svgPath);
        if (!r.isValid()) {
            qWarning("tintedIcon: invalid SVG '%s'", qPrintable(svgPath));
            return QPixmap();
        }
        const int s = 44;                       // 2x logical 22px, HiDPI-crisp
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        r.render(&p, QRectF(2, 2, s - 4, s - 4));
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
    icon.addPixmap(activePm, QIcon::Active,   QIcon::Off);
    icon.addPixmap(activePm, QIcon::Selected, QIcon::On);
    return icon;
}

} // namespace eddy::theme

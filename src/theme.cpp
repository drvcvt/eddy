#include "theme.h"
#include <QDebug>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

// Force the AUTORCC static-library resources to register at start-up.
// Without this call a static lib's qrc initialiser can be discarded by the
// linker when no other symbol from qrc_eddy.cpp.o is directly referenced.
static void initResources() { Q_INIT_RESOURCE(eddy); }
static const bool kResourcesInited = (initResources(), true);

namespace eddy::theme {

QPalette darkPalette() {
    QPalette p;
    const QColor bg("#16171d"), base("#101116"), text("#d8dae3"),
                 disabled("#6b6e7a"), btn("#1b1d24");
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, bg);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, btn);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Highlight, QColor(kAccent));
    p.setColor(QPalette::HighlightedText, QColor(kBar));
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return p;
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

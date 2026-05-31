// Dev tool: render the editor chrome to a PNG for visual verification (offscreen).
// Usage: QT_QPA_PLATFORM=offscreen ./build/eddy_preview /tmp/eddy-preview.png
#include "editorwindow.h"
#include "theme.h"
#include "config.h"
#include "cli.h"
#include <QApplication>
#include <QImage>
#include <QPixmap>
#include <QFile>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    app.setPalette(eddy::theme::darkPalette());
    QFile qss(":/eddy.qss");
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    QImage bg(900, 560, QImage::Format_ARGB32_Premultiplied);
    bg.fill(QColor("#243042"));
    eddy::Config cfg; eddy::CliOptions cli;
    cfg.animations = false;                  // static frame for a clean grab
    eddy::EditorWindow w(bg, cfg, cli);
    w.resize(900, 560);
    w.show();
    app.processEvents();
    QPixmap pm = w.grab();
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("/tmp/eddy-preview.png");
    pm.save(out);
    return 0;
}

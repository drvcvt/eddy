#include "cli.h"
#include "config.h"
#include "mediaio.h"
#include "compositor.h"
#include "editorwindow.h"
#include "theme.h"
#include <QApplication>
#include <cstdio>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QPalette systemPalette = app.palette();
    QApplication::setStyle("Fusion");
    app.setApplicationName("eddy");
    app.setDesktopFileName("eddy");

    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    auto pr = eddy::parseArgs(args);
    if (pr.exitNow) return pr.exitCode;
    if (!pr.ok) { std::fprintf(stderr, "eddy: %s\n", qPrintable(pr.error)); return 2; }

    auto load = eddy::loadMediaInput(pr.options.input);
    if (!load.ok) { std::fprintf(stderr, "eddy: %s\n", qPrintable(load.error)); return 1; }

    eddy::Config cfg = eddy::loadConfig(pr.options.configPath);
    eddy::applyCli(cfg, pr.options);
    const bool dark = eddy::theme::resolveDark(cfg.theme, systemPalette);
    app.setPalette(eddy::theme::palette(dark));
    app.setStyleSheet(eddy::theme::styleSheet(dark));

    eddy::pushWindowRules("eddy");   // before show → instant float, no fade
    eddy::EditorWindow win(load.document, cfg, pr.options);
    win.show();
    return app.exec();
}

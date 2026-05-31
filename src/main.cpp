#include "cli.h"
#include "config.h"
#include "imageio.h"
#include "compositor.h"
#include "editorwindow.h"
#include <QApplication>
#include <QFile>
#include <cstdio>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("eddy");
    app.setDesktopFileName("eddy");

    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    auto pr = eddy::parseArgs(args);
    if (pr.exitNow) return pr.exitCode;
    if (!pr.ok) { std::fprintf(stderr, "eddy: %s\n", qPrintable(pr.error)); return 2; }

    auto load = eddy::loadInput(pr.options.input);
    if (!load.ok) { std::fprintf(stderr, "eddy: %s\n", qPrintable(load.error)); return 1; }

    eddy::Config cfg = eddy::loadConfig(pr.options.configPath);
    eddy::applyCli(cfg, pr.options);

    QFile qss(":/eddy.qss");
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    eddy::pushWindowRules("eddy");   // before show → instant float, no fade
    eddy::EditorWindow win(load.image, cfg, pr.options);
    win.show();
    return app.exec();
}

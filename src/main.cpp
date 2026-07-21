#include "cli.h"
#include "config.h"
#include "mediaio.h"
#include "compositor.h"
#include "editorwindow.h"
#include "theme.h"
#include <QApplication>
#include <QFile>
#ifdef Q_OS_WIN
#include <QFileDialog>
#endif
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    app.setPalette(eddy::theme::darkPalette());
    app.setApplicationName("eddy");
    app.setDesktopFileName("eddy");

    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
#ifdef Q_OS_WIN
    if (args.isEmpty()) {
        const QString path = QFileDialog::getOpenFileName(
            nullptr,
            QStringLiteral("Open image or video"),
            {},
            QStringLiteral("Images and videos (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.mp4 *.m4v *.mov *.webm *.mkv *.avi);;All files (*.*)"));
        if (path.isEmpty())
            return 0;
        args << path;
    }
#endif
    auto pr = eddy::parseArgs(args);
    if (pr.exitNow) return pr.exitCode;
    if (!pr.ok) { std::fprintf(stderr, "eddy: %s\n", qPrintable(pr.error)); return 2; }

#ifdef Q_OS_WIN
    if (pr.options.input.kind == eddy::InputSpec::Stdin)
        _setmode(_fileno(stdin), _O_BINARY);
    if (pr.options.output.toStdout)
        _setmode(_fileno(stdout), _O_BINARY);
#endif

    auto load = eddy::loadMediaInput(pr.options.input);
    if (!load.ok) { std::fprintf(stderr, "eddy: %s\n", qPrintable(load.error)); return 1; }

    eddy::Config cfg = eddy::loadConfig(pr.options.configPath);
    eddy::applyCli(cfg, pr.options);

    QFile qss(":/eddy.qss");
    if (qss.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    eddy::pushWindowRules("eddy");   // before show → instant float, no fade
    eddy::EditorWindow win(load.document, cfg, pr.options);
    win.show();
    return app.exec();
}

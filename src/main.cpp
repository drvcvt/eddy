#include "cli.h"
#include "config.h"
#include "mediaio.h"
#include "compositor.h"
#include "editorwindow.h"
#include "theme.h"
#include <QApplication>
#ifdef Q_OS_WIN
#include <QFileDialog>
#include <fcntl.h>
#include <io.h>
#define NOMINMAX
#include <windows.h>
#endif
#include <cstdio>

#ifdef Q_OS_WIN
namespace {
// GUI-subsystem builds detach from the parent console, which would swallow
// help, version, and error output in cmd/PowerShell. Reattach, but leave
// streams alone that the parent already redirected to a pipe or file.
void attachParentConsole() {
    const auto redirected = [](DWORD stream) {
        const HANDLE handle = GetStdHandle(stream);
        return handle != nullptr && handle != INVALID_HANDLE_VALUE;
    };
    const bool stdoutRedirected = redirected(STD_OUTPUT_HANDLE);
    const bool stderrRedirected = redirected(STD_ERROR_HANDLE);
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    if (!stdoutRedirected) std::freopen("CONOUT$", "w", stdout);
    if (!stderrRedirected) std::freopen("CONOUT$", "w", stderr);
}
}
#endif

int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    attachParentConsole();
#endif
    QApplication app(argc, argv);
    const QPalette systemPalette = app.palette();
    QApplication::setStyle("Fusion");
    app.setApplicationName("eddy");
    app.setDesktopFileName("eddy");

    QStringList args = QCoreApplication::arguments().mid(1);
#ifdef Q_OS_WIN
    if (args.isEmpty()) {
        const QString path = QFileDialog::getOpenFileName(
            nullptr, QStringLiteral("Open image or video"), {},
            QStringLiteral("Images and videos (*.png *.jpg *.jpeg *.bmp *.gif *.webp "
                           "*.tif *.tiff *.mp4 *.m4v *.mov *.webm *.mkv *.avi);;"
                           "All files (*.*)"));
        if (path.isEmpty()) return 0;
        args << path;
    }
#endif
    auto pr = eddy::parseArgs(args);
    if (pr.exitNow) {
        if (pr.showHelp) std::fputs(qPrintable(eddy::helpText()), stdout);
        if (pr.showVersion) std::fprintf(stdout, "%s\n", qPrintable(eddy::versionString()));
        return pr.exitCode;
    }
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
    const bool dark = eddy::theme::resolveDark(cfg.theme, systemPalette);
    app.setPalette(eddy::theme::palette(dark));
    app.setStyleSheet(eddy::theme::styleSheet(dark));

    eddy::pushWindowRules("eddy");   // before show → instant float, no fade
    eddy::EditorWindow win(load.document, cfg, pr.options);
    win.show();
    return app.exec();
}

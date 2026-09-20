#include "cli.h"
#include <limits>

namespace eddy {

QString versionString() { return QStringLiteral("eddy " EDDY_VERSION); }

QString helpText() {
    return QStringLiteral(
        "usage: eddy [options] <image|video|->\n"
        "\n"
        "  -f, --file <path|->    input media ('-' reads an image from stdin)\n"
        "  -o, --output <path|->  write the saved image to a file or stdout\n"
        "      --save-dir <dir>   save into this directory with a timestamped name\n"
        "      --copy/--no-copy   force or suppress copying the result to the clipboard\n"
        "      --tool <name>      start with this tool selected\n"
        "      --config <path>    use this config file\n"
        "      --early-exit       exit after the first save\n"
        "      --no-anim          disable window and tool animations\n"
        "      --gpu              use the OpenGL canvas viewport\n"
        "      --boltsnap-card-id <id>  replace this Boltsnap card on save\n"
        "  -h, --help             show this help\n"
        "  -v, --version          show the version\n");
}

ParseResult parseArgs(const QStringList &args) {
    ParseResult r;
    CliOptions &o = r.options;
    bool haveInput = false;

    auto setInput = [&](const QString &v) {
        haveInput = true;
        if (v == "-") { o.input.kind = InputSpec::Stdin; o.input.path.clear(); }
        else { o.input.kind = InputSpec::File; o.input.path = v; }
    };

    for (int i = 0; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&](const QString &flag) -> QString {
            if (i + 1 >= args.size()) { r.ok = false; r.error = flag + " requires a value"; return {}; }
            return args[++i];
        };
        if (a == "-h" || a == "--help") {
            r.exitNow = true; r.showHelp = true; r.exitCode = 0; return r;
        } else if (a == "-v" || a == "--version") {
            r.exitNow = true; r.showVersion = true; r.exitCode = 0; return r;
        } else if (a == "-f" || a == "--file") {
            setInput(next(a)); if (!r.ok) return r;
        } else if (a == "-o" || a == "--output") {
            const QString v = next(a); if (!r.ok) return r;
            if (v == "-") o.output.toStdout = true;
            else { o.output.toFile = true; o.output.filePath = v; }
        } else if (a == "--save-dir") {
            o.output.saveDir = next(a); if (!r.ok) return r;
        } else if (a == "--copy") {
            o.output.copyToClipboard = true;
            o.output.copyFlagSet = true;
        } else if (a == "--no-copy") {
            o.output.copyToClipboard = false;
            o.output.copyFlagSet = true;
        } else if (a == "--tool") {
            o.startTool = next(a); if (!r.ok) return r;
        } else if (a == "--config") {
            o.configPath = next(a); if (!r.ok) return r;
        } else if (a == "--early-exit") {
            o.earlyExit = true;
        } else if (a == "--no-anim") {
            o.noAnim = true;
        } else if (a == "--gpu") {
            o.useGpuViewport = true;
        } else if (a == "--boltsnap-card-id") {
            const QString value = next(a); if (!r.ok) return r;
            bool ok = false;
            const quint64 id = value.toULongLong(&ok);
            if (!ok || id == 0 || id > quint64(std::numeric_limits<qint64>::max())) {
                r.ok = false; r.error = a + " requires a positive integer"; return r;
            }
            o.boltsnapCardId = id;
        } else if (a.startsWith('-') && a != "-") {
            r.ok = false; r.error = "unknown option: " + a; return r;
        } else {
            setInput(a); // positional or "-"
        }
    }

    if (!haveInput) { r.ok = false; r.error = "no input media (pass a path, -f FILE, or -)"; }
    return r;
}

}

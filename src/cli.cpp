#include "cli.h"

namespace eddy {

QString versionString() { return QStringLiteral("eddy 0.1.0"); }

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
            r.exitNow = true; r.exitCode = 0; return r;
        } else if (a == "-v" || a == "--version") {
            r.exitNow = true; r.exitCode = 0; return r;
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
        } else if (a == "--no-copy") {
            o.output.copyToClipboard = false;
        } else if (a == "--tool") {
            o.startTool = next(a); if (!r.ok) return r;
        } else if (a == "--config") {
            o.configPath = next(a); if (!r.ok) return r;
        } else if (a == "--early-exit") {
            o.earlyExit = true;
        } else if (a == "--gpu") {
            o.useGpuViewport = true;
        } else if (a.startsWith('-') && a != "-") {
            r.ok = false; r.error = "unknown option: " + a; return r;
        } else {
            setInput(a); // positional or "-"
        }
    }

    if (!haveInput) { r.ok = false; r.error = "no input image (pass a path, -f FILE, or -)"; }
    return r;
}

}

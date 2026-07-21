#pragma once
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace eddy {

QString versionString();

// Where the input image comes from.
struct InputSpec {
    enum Kind { File, Stdin } kind = File;
    QString path;            // valid when kind == File
};

// Where the result goes when the user saves.
struct OutputSpec {
    bool copyToClipboard = true;   // default action copies
    bool copyFlagSet = false;      // true once --copy/--no-copy was seen
    bool toFile = false;
    bool toStdout = false;
    QString filePath;              // valid when toFile
    QString saveDir;               // optional save-dir for the save action
};

struct CliOptions {
    InputSpec input;
    OutputSpec output;
    QString startTool;             // empty = config/default
    QString configPath;            // empty = default location
    bool earlyExit = false;
    bool noAnim = false;
    bool useGpuViewport = false;
    quint64 boltsnapCardId = 0;
};

// Result of parsing. ok==false means print `error` to stderr and exit 2.
struct ParseResult {
    bool ok = true;
    bool exitNow = false;          // --help/--version already handled
    int exitCode = 0;
    QString error;
    CliOptions options;
};

// args excludes argv[0].
ParseResult parseArgs(const QStringList &args);

}

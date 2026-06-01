#include "config.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

namespace eddy {

QString defaultConfigPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(base).filePath("eddy/config");
}

Config loadConfig(const QString &path) {
    Config c;
    c.saveDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    c.strokeColor = QColor("#ff3b30");

    const QString file = path.isEmpty() ? defaultConfigPath() : path;
    QSettings s(file, QSettings::IniFormat);
    s.beginGroup("eddy");
    c.defaultTool = s.value("default_tool", c.defaultTool).toString();
    c.lineWidth   = s.value("line_width", c.lineWidth).toDouble();
    c.saveDir     = s.value("save_dir", c.saveDir).toString();
    c.textFont    = s.value("text_font", c.textFont).toString();
    c.earlyExit   = s.value("early_exit", c.earlyExit).toBool();
    c.copyOnSave  = s.value("copy_on_save", c.copyOnSave).toBool();
    c.animations  = s.value("animations", c.animations).toBool();
    c.ocrLang     = s.value("ocr_lang", c.ocrLang).toString();
    c.ocrPsm      = s.value("ocr_psm", c.ocrPsm).toInt();
    if (s.contains("stroke_color"))
        c.strokeColor = QColor(s.value("stroke_color").toString());
    s.endGroup();
    return c;
}

void applyCli(Config &cfg, const CliOptions &cli) {
    if (!cli.startTool.isEmpty()) cfg.defaultTool = cli.startTool;
    if (!cli.output.saveDir.isEmpty()) cfg.saveDir = cli.output.saveDir;
    if (cli.output.copyFlagSet) cfg.copyOnSave = cli.output.copyToClipboard;
    if (cli.earlyExit) cfg.earlyExit = true;
    if (cli.noAnim) cfg.animations = false;
}

}

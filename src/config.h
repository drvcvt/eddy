#pragma once
#include <QColor>
#include <QString>
#include "cli.h"

namespace eddy {

struct Config {
    QString saveDir;                 // default: ~/Pictures
    QString defaultTool = "arrow";
    double lineWidth = 4.0;
    QColor strokeColor;              // initialized to #ff3b30 in loadConfig()
    QString textFont = "Sans";
    bool earlyExit = false;
    bool copyOnSave = true;
    bool animations = true;
};

// Loads from `path` (INI). If path is empty, uses the default location
// ~/.config/eddy/config. Missing file -> defaults.
Config loadConfig(const QString &path);

// CLI flags win over config.
void applyCli(Config &cfg, const CliOptions &cli);

QString defaultConfigPath();

}

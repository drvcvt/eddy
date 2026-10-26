#include "output/compositor.h"
#include <QProcess>
#include <QProcessEnvironment>

namespace eddy {

QStringList hyprlandRules(const QString &appId) {
    const QString sel = QString("class:^(%1)$").arg(appId);
    return {
        "noanim, "  + sel,
        "noblur, "  + sel,
        "noshadow, " + sel,
        "float, "   + sel,
        "center, "  + sel,
        "pin, "     + sel,
    };
}

QString hyprlandBatch(const QString &appId) {
    QStringList commands;
    for (const QString &rule : hyprlandRules(appId)) commands << QStringLiteral("keyword windowrulev2 ") + rule;
    return commands.join(QStringLiteral(" ; "));
}

void pushWindowRules(const QString &appId) {
    const auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains("HYPRLAND_INSTANCE_SIGNATURE")) {
        QProcess::execute("hyprctl", {"--batch", hyprlandBatch(appId)});
    }
    if (env.contains("SWAYSOCK")) {
        QProcess::execute("swaymsg",
            {"for_window", QString("[app_id=\"%1\"]").arg(appId), "floating enable"});
    }
}

}

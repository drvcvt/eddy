#include "compositor.h"
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

void pushWindowRules(const QString &appId) {
    const auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains("HYPRLAND_INSTANCE_SIGNATURE")) {
        for (const QString &rule : hyprlandRules(appId))
            QProcess::execute("hyprctl", {"keyword", "windowrulev2", rule});
    }
    if (env.contains("SWAYSOCK")) {
        QProcess::execute("swaymsg",
            {"for_window", QString("[app_id=\"%1\"]").arg(appId), "floating enable"});
    }
}

}

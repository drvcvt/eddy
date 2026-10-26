#pragma once
#include <QStringList>
namespace eddy {
// Pure: the windowrulev2 args eddy would push for a given app-id.
QStringList hyprlandRules(const QString &appId);
// Side-effectful: push rules if HYPRLAND_INSTANCE_SIGNATURE / SWAYSOCK present.
void pushWindowRules(const QString &appId);
}

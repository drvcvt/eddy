#pragma once
#include <QString>
#include <QStringList>
#include <QPixmap>

class QWidget;

namespace eddy {

// Starts a native Wayland file drag when Eddy is running on a Wayland platform.
// Returns false on non-Wayland sessions or when the compositor/native handles
// are unavailable, so callers can fall back to Qt's QDrag path.
bool startWaylandFileDrag(QWidget *origin, const QString &path, const QStringList &mimeTypes,
                          const QPixmap &ghost = QPixmap());

}

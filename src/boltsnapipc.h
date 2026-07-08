#pragma once
#include "exporter.h"
#include <QByteArray>
#include <QString>

namespace eddy {

QByteArray buildBoltsnapAddFrame(const QByteArray &png, const QString &source);
QString boltsnapSocketPath();
DeliverResult sendPngToBoltsnapShelf(const QByteArray &png, const QString &source);

}

#pragma once
#include "exporter.h"
#include <QByteArray>
#include <QString>

namespace eddy {

QByteArray buildBoltsnapAddFrame(const QByteArray &png, const QString &source);
QByteArray buildBoltsnapReloadFrame(quint64 cardId);
QString boltsnapSocketPath();
DeliverResult sendPngToBoltsnapShelf(const QByteArray &png, const QString &source);
DeliverResult reloadBoltsnapShelfCard(quint64 cardId);

}

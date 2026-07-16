#pragma once
#include "exporter.h"
#include <QByteArray>
#include <QString>

namespace eddy {

QByteArray buildBoltsnapAddFrame(const QByteArray &png, const QString &source,
                                 const QString &output = {});
QByteArray buildBoltsnapImageReplaceFrame(quint64 id, const QByteArray &png);
QByteArray buildBoltsnapVideoReplaceFrame(quint64 id, const QString &path);
QByteArray buildBoltsnapVideoAddFrame(const QString &path, const QString &source,
                                      bool takeOwnership, const QString &output = {});
QString boltsnapSocketPath();
DeliverResult sendPngToBoltsnapShelf(const QByteArray &png, const QString &source,
                                     const QString &output = {});
DeliverResult sendPngToBoltsnapCard(quint64 id, const QByteArray &png);
DeliverResult sendVideoToBoltsnapCard(quint64 id, const QString &path);
DeliverResult sendVideoToBoltsnapShelf(const QString &path, const QString &source,
                                       bool takeOwnership, const QString &output = {});

}

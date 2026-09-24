#pragma once
#include <QGraphicsItem>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QRect>
#include <optional>
#include "exportsettings.h"
#include "mediaio.h"
#include "studiodocument.h"

namespace eddy {

inline constexpr int kProjectFormatVersion = 1;

// An editable Eddy document (21.09. plan 5): the source kept as an asset next
// to the manifest, every annotation and edit, never a rendered result and no
// undo history.
struct ProjectSnapshot {
    MediaKind kind = MediaKind::Image;
    QString asset;             // file name inside the project's assets folder
    QString sourceName;        // the original's file name, for messages
    QString sha256;            // hex digest of the asset's bytes
    qint64 assetSize = 0;
    QSize size;                // document pixels
    qint64 durationMs = 0;     // videos only
    QRect crop;
    qint64 trimInMs = 0, trimOutMs = -1;
    qint64 positionMs = 0;
    StudioDocument studio;
    ExportSettings exportSettings;
    QJsonArray items;          // checked by itemsFromJson before any item exists
};

QJsonObject projectToJson(const ProjectSnapshot &project);
// The whole manifest or nothing, with the reason.
std::optional<ProjectSnapshot> projectFromJson(const QJsonObject &json, QString *error);

// Annotations in stacking order; the background, handles and other overlays
// are left out.
QJsonArray itemsToJson(const QList<QGraphicsItem *> &itemsInStackingOrder);
// The annotations for a document of `canvas` size; `background` feeds
// redactions. Nothing is created unless every entry is valid.
std::optional<QList<QGraphicsItem *>> itemsFromJson(const QJsonArray &items, const QImage &background,
                                                   QSize canvas, QString *error);

}

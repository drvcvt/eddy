#pragma once
#include <QImage>
#include <QString>
#include <functional>
#include "exporter.h"
#include "projectcodec.h"

namespace eddy {

// `name.eddy` keeps its assets in `name.eddy.assets` beside it.
QString projectAssetsDir(const QString &manifestPath);
// Whether `path` is a project's manifest or lies in any assets folder; normal
// output must never write there.
bool isProjectPath(const QString &path);

struct AssetResult {
    bool ok = false;
    QString error;
    QString name;      // file name inside the assets folder
    QString sha256;    // hex digest of the bytes
    qint64 size = 0;
};

// Copies `source` into `assetsDir`, hashing while it copies, under a name
// derived from the digest; an identical asset already there is kept. A cursor
// track beside the source travels along. Safe on any thread.
AssetResult storeAsset(const QString &source, const QString &assetsDir,
                       const std::function<void(int percent)> &progress = {},
                       const std::function<bool()> &cancelled = {});
// Images without a file (stdin, clipboard) are kept as lossless PNG.
AssetResult storeImageAsset(const QImage &image, const QString &assetsDir);

// The manifest only replaces an old one once it is complete.
DeliverResult writeProject(const QString &manifestPath, const ProjectSnapshot &project);

struct OpenedProject {
    bool ok = false;
    QString error;
    ProjectSnapshot snapshot;
    QString sourcePath;   // the asset, checked to exist with the recorded size
    bool originalMissing = false;   // the manifest is fine; its original is gone or changed
};
OpenedProject openProject(const QString &manifestPath);
// "Locate original": puts `candidate` back as the project's original, but only
// when its bytes are the recorded ones (SHA-256), never a look-alike.
DeliverResult relinkProjectSource(const QString &manifestPath, const QString &candidate);

}

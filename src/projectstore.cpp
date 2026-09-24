#include "projectstore.h"
#include "cursortrack.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryFile>

namespace eddy {

static constexpr qint64 kMaxManifest = 64ll * 1024 * 1024;

QString projectAssetsDir(const QString &manifestPath) {
    return manifestPath + QStringLiteral(".assets");
}

bool isProjectPath(const QString &path) {
    const QFileInfo info(path);
    if (info.suffix().compare(QLatin1String("eddy"), Qt::CaseInsensitive) == 0) return true;
    for (QDir dir = info.absoluteDir(); !dir.isRoot(); dir = QFileInfo(dir.absolutePath()).absoluteDir())
        if (dir.dirName().endsWith(QLatin1String(".eddy.assets"))) return true;
    return false;
}

AssetResult storeAsset(const QString &source, const QString &assetsDir,
                       const std::function<void(int)> &progress, const std::function<bool()> &cancelled) {
    AssetResult r;
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("cannot read %1").arg(source);
        return r;
    }
    if (!QDir().mkpath(assetsDir)) {
        r.error = QStringLiteral("cannot create %1").arg(assetsDir);
        return r;
    }
    // Copy under a temporary name, hashing on the way; name it by its digest.
    QTemporaryFile out(QDir(assetsDir).filePath(QStringLiteral(".copy-XXXXXX")));
    if (!out.open()) {
        r.error = QStringLiteral("cannot write in %1").arg(assetsDir);
        return r;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 total = qMax<qint64>(1, in.size());
    QByteArray chunk;
    int reported = -1;
    while (!(chunk = in.read(4 * 1024 * 1024)).isEmpty()) {
        if (cancelled && cancelled()) {
            r.error = QStringLiteral("project save cancelled");
            return r;
        }
        hash.addData(chunk);
        if (out.write(chunk) != chunk.size()) {
            r.error = QStringLiteral("cannot write %1: %2").arg(out.fileName(), out.errorString());
            return r;
        }
        r.size += chunk.size();
        const int percent = int(r.size * 100 / total);
        if (progress && percent != reported) progress(reported = percent);
    }
    if (in.error() != QFile::NoError || !out.flush()) {
        r.error = QStringLiteral("cannot copy %1").arg(source);
        return r;
    }
    r.sha256 = QString::fromLatin1(hash.result().toHex());
    const QString suffix = QFileInfo(source).suffix().toLower();
    r.name = QStringLiteral("source-%1%2").arg(r.sha256.left(16),
                                              suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix);
    const QString target = QDir(assetsDir).filePath(r.name);
    if (QFileInfo(target).size() == r.size) {
        r.ok = true;   // the same bytes are already there
    } else {
        QFile::remove(target);
        out.setAutoRemove(false);
        out.close();
        if (!QFile::rename(out.fileName(), target)) {
            QFile::remove(out.fileName());
            r.error = QStringLiteral("cannot place %1").arg(target);
            return r;
        }
        r.ok = true;
    }
    // The cursor track follows the video under the asset's own base name.
    const QString track = cursorTrackPathFor(source);
    if (QFileInfo::exists(track)) {
        const QString trackTarget = cursorTrackPathFor(target);
        QFile::remove(trackTarget);
        QFile::copy(track, trackTarget);
    }
    return r;
}

AssetResult storeImageAsset(const QImage &image, const QString &assetsDir) {
    AssetResult r;
    if (!QDir().mkpath(assetsDir)) {
        r.error = QStringLiteral("cannot create %1").arg(assetsDir);
        return r;
    }
    QTemporaryFile png(QDir(assetsDir).filePath(QStringLiteral(".image-XXXXXX.png")));
    if (!png.open()) {
        r.error = QStringLiteral("cannot write in %1").arg(assetsDir);
        return r;
    }
    const QByteArray bytes = encodePng(image);
    if (bytes.isEmpty() || png.write(bytes) != bytes.size() || !png.flush()) {
        r.error = QStringLiteral("cannot write the image asset");
        return r;
    }
    png.close();
    return storeAsset(png.fileName(), assetsDir);
}

DeliverResult writeProject(const QString &manifestPath, const ProjectSnapshot &project) {
    DeliverResult r;
    QSaveFile file(manifestPath);
    file.setDirectWriteFallback(false);
    const QByteArray json = QJsonDocument(projectToJson(project)).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        r.error = QStringLiteral("cannot save %1: %2").arg(manifestPath, file.errorString());
        return r;
    }
    r.ok = true;
    r.path = manifestPath;
    return r;
}

static std::optional<ProjectSnapshot> readManifest(const QString &manifestPath, QString *error) {
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(manifestPath);
        return std::nullopt;
    }
    if (file.size() > kMaxManifest) {
        *error = QStringLiteral("%1 is too large for a project").arg(manifestPath);
        return std::nullopt;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (!doc.isObject()) {
        *error = QStringLiteral("%1 is not valid JSON: %2").arg(manifestPath, parse.errorString());
        return std::nullopt;
    }
    return projectFromJson(doc.object(), error);
}

DeliverResult relinkProjectSource(const QString &manifestPath, const QString &candidate) {
    DeliverResult r;
    const auto project = readManifest(manifestPath, &r.error);
    if (!project) return r;
    QFile file(candidate);
    if (!file.open(QIODevice::ReadOnly)) {
        r.error = QStringLiteral("cannot read %1").arg(candidate);
        return r;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || QString::fromLatin1(hash.result().toHex()) != project->sha256) {
        r.error = QStringLiteral("%1 is not this project's original").arg(QFileInfo(candidate).fileName());
        return r;
    }
    const AssetResult asset = storeAsset(candidate, projectAssetsDir(manifestPath));
    if (!asset.ok) {
        r.error = asset.error;
        return r;
    }
    ProjectSnapshot relinked = *project;
    relinked.asset = asset.name;
    return writeProject(manifestPath, relinked);
}

OpenedProject openProject(const QString &manifestPath) {
    OpenedProject p;
    const auto snapshot = readManifest(manifestPath, &p.error);
    if (!snapshot) return p;
    p.snapshot = *snapshot;
    p.sourcePath = QDir(projectAssetsDir(manifestPath)).filePath(p.snapshot.asset);
    const QFileInfo asset(p.sourcePath);
    if (!asset.exists()) {
        p.error = QStringLiteral("the project's original is missing: %1").arg(p.sourcePath);
        return p;
    }
    if (asset.size() != p.snapshot.assetSize) {
        p.error = QStringLiteral("the project's original changed: %1").arg(p.sourcePath);
        return p;
    }
    p.ok = true;
    return p;
}

}

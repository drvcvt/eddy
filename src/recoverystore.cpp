#include "recoverystore.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>

namespace eddy {

QString RecoveryStore::defaultRoot() {
    const QByteArray env = qgetenv("EDDY_RECOVERY_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("eddy/recovery"));
}

RecoveryStore::RecoveryStore(QString root) : m_root(std::move(root)) {}

QString RecoveryStore::create() const {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir().mkpath(QDir(m_root).filePath(id)) ? id : QString();
}

QString RecoveryStore::manifestFor(const QString &id) const {
    return QDir(QDir(m_root).filePath(id)).filePath(QStringLiteral("project.eddy"));
}

QString RecoveryStore::lockFileFor(const QString &id) const {
    return QDir(QDir(m_root).filePath(id)).filePath(QStringLiteral("lock"));
}

bool RecoveryStore::touch(const QString &id, const QString &source, const QString &name) const {
    QSaveFile file(QDir(QDir(m_root).filePath(id)).filePath(QStringLiteral("entry.json")));
    file.setDirectWriteFallback(false);
    const QByteArray json = QJsonDocument(QJsonObject{
        {"source", source}, {"name", name},
        {"updated", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}}).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
}

static qint64 folderBytes(const QString &dir) {
    qint64 bytes = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) bytes += QFileInfo(it.next()).size();
    return bytes;
}

QVector<RecoveryStore::Entry> RecoveryStore::entries() const {
    QVector<Entry> out;
    for (const QFileInfo &dir : QDir(m_root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile file(QDir(dir.absoluteFilePath()).filePath(QStringLiteral("entry.json")));
        if (!file.open(QIODevice::ReadOnly) || !QFileInfo::exists(manifestFor(dir.fileName()))) continue;
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        Entry e;
        e.id = dir.fileName();
        e.manifest = manifestFor(e.id);
        e.source = o.value("source").toString();
        e.name = o.value("name").toString();
        e.updated = QDateTime::fromString(o.value("updated").toString(), Qt::ISODateWithMs);
        e.bytes = folderBytes(dir.absoluteFilePath());
        QLockFile lock(lockFileFor(e.id));
        e.inUse = !lock.tryLock(0);
        out.append(e);
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.updated > b.updated; });
    return out;
}

bool RecoveryStore::discard(const QString &id) const {
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.startsWith(QLatin1Char('.'))) return false;
    {
        QLockFile lock(lockFileFor(id));
        if (!lock.tryLock(0)) return false;
    }
    return QDir(QDir(m_root).filePath(id)).removeRecursively();
}

qint64 RecoveryStore::usedBytes() const {
    return folderBytes(m_root);
}

}

#pragma once
#include <QDateTime>
#include <QString>
#include <QVector>

namespace eddy {

// Kept edits for Resume (21.09. plan 5): one folder per edited document, each
// an ordinary project (project.eddy plus its assets) and a small entry.json.
// Lives in the app data folder, never in /tmp.
class RecoveryStore {
public:
    struct Entry {
        QString id;
        QString manifest;      // the entry's project.eddy
        QString source;        // where the original was opened from
        QString name;          // its file name, for the list
        QDateTime updated;
        qint64 bytes = 0;      // everything the entry keeps on disk
        bool inUse = false;    // an open window holds its lock
    };

    // EDDY_RECOVERY_DIR, else the app data folder's "recovery".
    static QString defaultRoot();
    explicit RecoveryStore(QString root = defaultRoot());

    QString root() const { return m_root; }
    // A new, empty entry; returns its id.
    QString create() const;
    QString manifestFor(const QString &id) const;
    QString lockFileFor(const QString &id) const;
    // Records what the entry is about; called after each snapshot.
    bool touch(const QString &id, const QString &source, const QString &name) const;
    QVector<Entry> entries() const;   // newest first
    // An entry an open window holds cannot be discarded.
    bool discard(const QString &id) const;
    qint64 usedBytes() const;

    // New source copies stop once this much is kept; existing entries stay.
    static constexpr qint64 kBudgetBytes = 2ll * 1024 * 1024 * 1024;

private:
    QString m_root;
};

}

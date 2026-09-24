#include <QtTest>
#include <QLockFile>
#include <QTemporaryDir>
#include "recoverystore.h"

using namespace eddy;

static void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
}

class TestRecoveryStore : public QObject {
    Q_OBJECT
private slots:
    void keepsEntriesNewestFirstAndDiscardsOnlyFreeOnes() {
        QTemporaryDir dir;
        RecoveryStore store(dir.path());
        QVERIFY(store.entries().isEmpty());
        const QString older = store.create(), newer = store.create();
        QVERIFY(!older.isEmpty() && older != newer);
        for (const QString &id : {older, newer}) write(store.manifestFor(id), "{}");
        QVERIFY(store.touch(older, "/pics/a.png", "a.png"));
        QTest::qWait(5);
        QVERIFY(store.touch(newer, "/pics/b.mp4", "b.mp4"));
        const auto entries = store.entries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].name, QStringLiteral("b.mp4"));
        QCOMPARE(entries[1].source, QStringLiteral("/pics/a.png"));
        QVERIFY(store.usedBytes() > 0);
        // A window holding the lock keeps its entry.
        QLockFile held(store.lockFileFor(newer));
        QVERIFY(held.tryLock(0));
        QVERIFY(store.entries()[0].inUse);
        QVERIFY(!store.discard(newer));
        held.unlock();
        QVERIFY(store.discard(newer));
        QCOMPARE(store.entries().size(), 1);
        QVERIFY(!store.discard("../escape"));
    }
    void anEntryWithoutASnapshotIsNotListed() {
        QTemporaryDir dir;
        RecoveryStore store(dir.path());
        const QString id = store.create();
        QVERIFY(store.touch(id, "/pics/a.png", "a.png"));
        QVERIFY(store.entries().isEmpty());   // nothing to resume yet
    }
};

QTEST_GUILESS_MAIN(TestRecoveryStore)
#include "test_recoverystore.moc"

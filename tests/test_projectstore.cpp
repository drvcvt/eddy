#include <QtTest>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include "projectstore.h"

using namespace eddy;

static QString write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    return path;
}

class TestProjectStore : public QObject {
    Q_OBJECT
private slots:
    void storesAHashedCopyWithItsCursorTrack() {
        QTemporaryDir dir;
        const QByteArray bytes(5 * 1024 * 1024 + 17, 'x');
        const QString source = write(dir.filePath("clip.mp4"), bytes);
        write(dir.filePath("clip.cursor.json"), "{}");
        const QString assets = projectAssetsDir(dir.filePath("demo.eddy"));
        QVector<int> steps;
        const AssetResult a = storeAsset(source, assets, [&](int p) { steps << p; });
        QVERIFY2(a.ok, qPrintable(a.error));
        QCOMPARE(a.sha256, QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
        QCOMPARE(a.size, qint64(bytes.size()));
        QVERIFY(a.name.endsWith(".mp4"));
        QCOMPARE(QFileInfo(QDir(assets).filePath(a.name)).size(), qint64(bytes.size()));
        QVERIFY(QFileInfo::exists(cursorTrackPathFor(QDir(assets).filePath(a.name))));
        QCOMPARE(steps.last(), 100);
        // Storing it again keeps the one asset.
        const AssetResult again = storeAsset(source, assets);
        QCOMPARE(again.name, a.name);
        QCOMPARE(QDir(assets).entryList(QDir::Files | QDir::Hidden).size(), 2);
    }
    void writesAndOpensAProject() {
        QTemporaryDir dir;
        const QString manifest = dir.filePath("demo.eddy");
        const AssetResult a = storeAsset(write(dir.filePath("shot.png"), "png-bytes"), projectAssetsDir(manifest));
        ProjectSnapshot p;
        p.asset = a.name;
        p.sha256 = a.sha256;
        p.assetSize = a.size;
        p.sourceName = "shot.png";
        p.size = QSize(10, 10);
        QVERIFY(writeProject(manifest, p).ok);
        const OpenedProject opened = openProject(manifest);
        QVERIFY2(opened.ok, qPrintable(opened.error));
        QCOMPARE(opened.sourcePath, QDir(projectAssetsDir(manifest)).filePath(a.name));
        // A changed or missing original is reported, not silently used.
        write(opened.sourcePath, "other");
        QVERIFY(!openProject(manifest).ok);
        QFile::remove(opened.sourcePath);
        QVERIFY(openProject(manifest).error.contains("missing"));
    }
    void locatingTheOriginalChecksItsBytes() {
        QTemporaryDir dir;
        const QString manifest = dir.filePath("demo.eddy");
        const QString original = write(dir.filePath("shot.png"), "the-original");
        const AssetResult a = storeAsset(original, projectAssetsDir(manifest));
        ProjectSnapshot p;
        p.asset = a.name; p.sha256 = a.sha256; p.assetSize = a.size; p.size = QSize(4, 4);
        QVERIFY(writeProject(manifest, p).ok);
        QVERIFY(QFile::remove(QDir(projectAssetsDir(manifest)).filePath(a.name)));
        QVERIFY(!openProject(manifest).ok);
        QVERIFY(openProject(manifest).originalMissing);
        // A broken manifest is not a missing original, whatever its name says.
        const QString broken = write(dir.filePath("changed-missing.eddy"), "{ not json");
        QVERIFY(!openProject(broken).ok);
        QVERIFY(!openProject(broken).originalMissing);
        const DeliverResult wrong = relinkProjectSource(manifest, write(dir.filePath("other.png"), "look-alike"));
        QVERIFY(!wrong.ok && wrong.error.contains("not this project's original"));
        QVERIFY(relinkProjectSource(manifest, original).ok);
        QVERIFY(openProject(manifest).ok);
    }
    void aFailedSaveLeavesTheOldProject() {
        QTemporaryDir dir;
        const QString manifest = dir.filePath("demo.eddy");
        ProjectSnapshot p;
        p.asset = "a.png"; p.sha256 = QString(64, 'c'); p.size = QSize(4, 4);
        QVERIFY(writeProject(manifest, p).ok);
        const QByteArray before = [&] { QFile f(manifest); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        QVERIFY(!writeProject(dir.filePath("missing/dir/other.eddy"), p).ok);
        QFile f(manifest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), before);
    }
    void recognisesProjectPaths() {
        QVERIFY(isProjectPath("/x/demo.eddy"));
        QVERIFY(isProjectPath("/x/demo.eddy.assets/source-1.mp4"));
        QVERIFY(!isProjectPath("/x/out.mp4"));
    }
};

QTEST_GUILESS_MAIN(TestProjectStore)
#include "test_projectstore.moc"

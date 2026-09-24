#include <QtTest>
#include <QTemporaryDir>
#include "studiopresets.h"

using namespace eddy;

static StudioPreset dusk() {
    StudioPreset p;
    p.name = QStringLiteral("Dusk / wide");
    p.style.background = StudioStyle::Background::Gradient;
    p.style.color = QColor("#5b4bdb");
    p.style.color2 = QColor("#ff7eb3");
    p.style.padding = 12;
    p.style.aspect = QSize(16, 9);
    p.motion = ZoomSegment::Motion::Smooth;
    return p;
}

class TestStudioPresets : public QObject {
    Q_OBJECT
private slots:
    void savedPresetsComeBackByName() {
        QTemporaryDir dir;
        const QString config = dir.filePath(QStringLiteral("config"));
        QVERIFY(loadStudioPresets(config).isEmpty());
        StudioPreset second = dusk();
        second.name = QStringLiteral("Anthracite");
        second.style.background = StudioStyle::Background::Color;
        QVERIFY(saveStudioPreset(config, dusk()).ok);
        QVERIFY(saveStudioPreset(config, second).ok);
        const auto presets = loadStudioPresets(config);
        QCOMPARE(presets.size(), 2);
        QCOMPARE(presets[0], second);
        QCOMPARE(presets[1], dusk());
        // A slash in the name never becomes a path.
        QCOMPARE(QDir(studioPresetsDir(config)).entryList(QDir::Files).size(), 2);
        QVERIFY(QDir(studioPresetsDir(config)).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
        // Names that clean up alike stay two presets.
        for (const char *name : {"Blue/Dark", "Blue_Dark", "\u9752", "\u8d64"}) {
            StudioPreset p = second;
            p.name = QString::fromUtf8(name);
            QVERIFY(saveStudioPreset(config, p).ok);
        }
        QCOMPARE(loadStudioPresets(config).size(), 6);
    }
    void anOddImageTypeIsReencoded() {
        QTemporaryDir dir;
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::red);
        const QString misnamed = dir.filePath(QStringLiteral("bg.png"));
        QVERIFY(image.save(misnamed, "BMP"));
        StudioPreset p = dusk();
        p.style.background = StudioStyle::Background::Image;
        p.style.imagePath = misnamed;
        QString error;
        const QJsonObject json = QJsonDocument::fromJson(exportStudioPreset(p, &error)).object();
        const QJsonObject embedded = json.value("style").toObject().value("image").toObject();
        QCOMPARE(embedded.value("type").toString(), QStringLiteral("jpeg"));
        QVERIFY(QByteArray::fromBase64(embedded.value("data").toString().toLatin1()).startsWith("\xff\xd8"));
    }
    void anExportedPresetCarriesItsImage() {
        QTemporaryDir dir, data;
        qputenv("EDDY_DATA_DIR", data.path().toLocal8Bit());
        const auto unset = qScopeGuard([] { qunsetenv("EDDY_DATA_DIR"); });
        QImage image(64, 32, QImage::Format_RGB32);
        image.fill(QColor(10, 200, 30));
        const QString png = dir.filePath(QStringLiteral("bg.png"));
        QVERIFY(image.save(png));
        StudioPreset p = dusk();
        p.style.background = StudioStyle::Background::Image;
        p.style.imagePath = png;
        QString error;
        const QByteArray file = exportStudioPreset(p, &error);
        QVERIFY2(!file.isEmpty(), qPrintable(error));
        QVERIFY(QFile::remove(png));   // the receiver does not have it
        const auto imported = importStudioPreset(file, &error);
        QVERIFY2(imported, qPrintable(error));
        QVERIFY(imported->style.imagePath.startsWith(studioBackgroundsDir()));
        QCOMPARE(QImage(imported->style.imagePath).pixelColor(5, 5), QColor(10, 200, 30));
        QCOMPARE(imported->motion, ZoomSegment::Motion::Smooth);
        // Importing again keeps one stored image.
        QVERIFY(importStudioPreset(file, &error));
        QCOMPARE(QDir(studioBackgroundsDir()).entryList(QDir::Files).size(), 1);
    }
    void refusesBrokenAndNewerPresets() {
        QString error;
        QVERIFY(!importStudioPreset("{ nope", &error));
        QByteArray newer = exportStudioPreset(dusk(), &error);
        newer.replace("\"version\": 1", "\"version\": 2");
        QVERIFY(!importStudioPreset(newer, &error));
        QVERIFY(error.contains(QStringLiteral("version")));
        QVERIFY(!importStudioPreset(QByteArray(17 * 1024 * 1024, ' '), &error));
    }
};

QTEST_MAIN(TestStudioPresets)
#include "test_studiopresets.moc"

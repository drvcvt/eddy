#include <QtTest>
#include <QTemporaryDir>
#include "exportsettings.h"

using namespace eddy;

class TestExportSettings : public QObject {
    Q_OBJECT
private slots:
    void presetsSetEveryRow() {
        QCOMPARE(exportPreset(ExportPreset::Original), ExportSettings{});
        const ExportSettings web = exportPreset(ExportPreset::Web);
        QCOMPARE(web.format, ExportSettings::Format::Mp4);
        QCOMPARE(web.shortSide, 1080);
        QCOMPARE(web.fps, 60);
        const ExportSettings small = exportPreset(ExportPreset::Small);
        QCOMPARE(small.shortSide, 720);
        QCOMPARE(small.fps, 30);
        const ExportSettings gif = exportPreset(ExportPreset::Gif);
        QCOMPARE(gif.format, ExportSettings::Format::Gif);
        QCOMPARE(gif.shortSide, 480);
        QCOMPARE(gif.fps, 15);
        // A changed row leaves every preset.
        QCOMPARE(presetOf(web), std::optional<ExportPreset>(ExportPreset::Web));
        ExportSettings custom = web;
        custom.fps = 15;
        QVERIFY(!presetOf(custom));
    }
    void suffixFollowsTheFormat() {
        QCOMPARE(exportSuffix(ExportSettings{}, QStringLiteral("/a/clip.mov")), QStringLiteral("mov"));
        QCOMPARE(exportSuffix(ExportSettings{}, QStringLiteral("/a/clip")), QStringLiteral("mp4"));
        QCOMPARE(exportSuffix(exportPreset(ExportPreset::Gif), QStringLiteral("/a/clip.mov")), QStringLiteral("gif"));
        ExportSettings webm;
        webm.format = ExportSettings::Format::WebM;
        QCOMPARE(exportSuffix(webm, QStringLiteral("/a/clip.mp4")), QStringLiteral("webm"));
    }
    void sizesShrinkToEvenPixelsAndNeverGrow() {
        QCOMPARE(exportSize(QSize(1920, 1080), 0), QSize(1920, 1080));
        QCOMPARE(exportSize(QSize(1920, 1080), 720), QSize(1280, 720));
        QCOMPARE(exportSize(QSize(1080, 1920), 480), QSize(480, 854));
        QCOMPARE(exportSize(QSize(640, 360), 1080), QSize(640, 360));   // no upscale
        QCOMPARE(exportSize(QSize(2058, 1190), 720), QSize(1246, 720));
    }
    void theLastChoiceIsRemembered() {
        QTemporaryDir dir;
        const QString config = dir.filePath(QStringLiteral("config"));
        QCOMPARE(loadExportSettings(config), ExportSettings{});
        saveExportSettings(config, exportPreset(ExportPreset::Small));
        QCOMPARE(loadExportSettings(config), exportPreset(ExportPreset::Small));
    }
};

QTEST_GUILESS_MAIN(TestExportSettings)
#include "test_exportsettings.moc"

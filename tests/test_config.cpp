#include <QtTest>
#include <QTemporaryFile>
#include <QTextStream>
#include "config.h"

using namespace eddy;

class TestConfig : public QObject {
    Q_OBJECT
private slots:
    void missingFileGivesDefaults() {
        Config c = loadConfig("/nonexistent/eddy/config");
        QCOMPARE(c.defaultTool, QString("arrow"));
        QCOMPARE(c.lineWidth, 4.0);
    }
    void readsValuesFromIni() {
        QTemporaryFile f; QVERIFY(f.open());
        {
            QTextStream ts(&f);
            ts << "[eddy]\n"
               << "default_tool=rect\n"
               << "line_width=8\n"
               << "save_dir=/tmp/shots\n";
        }
        f.flush();
        Config c = loadConfig(f.fileName());
        QCOMPARE(c.defaultTool, QString("rect"));
        QCOMPARE(c.lineWidth, 8.0);
        QCOMPARE(c.saveDir, QString("/tmp/shots"));
    }
    void iniCopyOnSaveSurvivesWithoutCliFlag() {
        Config c; c.copyOnSave = false;          // as if INI said copy_on_save=false
        CliOptions cli;                          // default: copyFlagSet=false
        applyCli(c, cli);
        QVERIFY(!c.copyOnSave);                  // INI value preserved
    }
    void cliOverridesConfig() {
        Config c; c.defaultTool = "rect"; c.copyOnSave = true;
        CliOptions cli;
        cli.startTool = "blur";
        cli.output.copyToClipboard = false;
        cli.output.copyFlagSet = true;
        cli.output.saveDir = "/tmp/x";
        applyCli(c, cli);
        QCOMPARE(c.defaultTool, QString("blur"));
        QVERIFY(!c.copyOnSave);
        QCOMPARE(c.saveDir, QString("/tmp/x"));
    }
    void animationsDefaultTrueAndIniOff() {
        Config d = loadConfig("/nonexistent/eddy/config");
        QVERIFY(d.animations);
        QTemporaryFile f; QVERIFY(f.open());
        { QTextStream ts(&f); ts << "[eddy]\nanimations=false\n"; }
        f.flush();
        Config c = loadConfig(f.fileName());
        QVERIFY(!c.animations);
    }
    void noAnimCliDisablesAnimations() {
        Config c; CliOptions cli; cli.noAnim = true;
        applyCli(c, cli);
        QVERIFY(!c.animations);
    }
    void ocrDefaultsAndIniOverride() {
        Config d = loadConfig("/nonexistent/eddy/config");
        QCOMPARE(d.ocrLang, QString("deu"));
        QCOMPARE(d.ocrPsm, 6);
        QTemporaryFile f; QVERIFY(f.open());
        { QTextStream ts(&f); ts << "[eddy]\nocr_lang=deu+eng\nocr_psm=11\n"; }
        f.flush();
        Config c = loadConfig(f.fileName());
        QCOMPARE(c.ocrLang, QString("deu+eng"));
        QCOMPARE(c.ocrPsm, 11);
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"

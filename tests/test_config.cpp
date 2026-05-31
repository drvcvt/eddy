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
        QVERIFY(c.lineWidth > 0.0);
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
    void cliOverridesConfig() {
        Config c; c.defaultTool = "rect"; c.copyOnSave = true;
        CliOptions cli;
        cli.startTool = "blur";
        cli.output.copyToClipboard = false;
        cli.output.saveDir = "/tmp/x";
        applyCli(c, cli);
        QCOMPARE(c.defaultTool, QString("blur"));
        QVERIFY(!c.copyOnSave);
        QCOMPARE(c.saveDir, QString("/tmp/x"));
    }
};

QTEST_GUILESS_MAIN(TestConfig)
#include "test_config.moc"

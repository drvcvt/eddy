#include <QtTest>
#include "cli.h"

using namespace eddy;

class TestCli : public QObject {
    Q_OBJECT
private slots:
    void positionalFile() {
        auto r = parseArgs({"shot.png"});
        QVERIFY(r.ok);
        QCOMPARE(r.options.input.kind, InputSpec::File);
        QCOMPARE(r.options.input.path, QString("shot.png"));
        QVERIFY(r.options.output.copyToClipboard); // default
    }
    void swappyFileAlias() {
        auto r = parseArgs({"-f", "shot.png"});
        QVERIFY(r.ok);
        QCOMPARE(r.options.input.path, QString("shot.png"));
    }
    void stdinDash() {
        auto r = parseArgs({"-f", "-"});
        QVERIFY(r.ok);
        QCOMPARE(r.options.input.kind, InputSpec::Stdin);
    }
    void outputFile() {
        auto r = parseArgs({"shot.png", "-o", "out.png"});
        QVERIFY(r.ok);
        QVERIFY(r.options.output.toFile);
        QCOMPARE(r.options.output.filePath, QString("out.png"));
    }
    void outputStdout() {
        auto r = parseArgs({"shot.png", "-o", "-"});
        QVERIFY(r.ok);
        QVERIFY(r.options.output.toStdout);
    }
    void noCopy() {
        auto r = parseArgs({"shot.png", "--no-copy"});
        QVERIFY(r.ok);
        QVERIFY(!r.options.output.copyToClipboard);
    }
    void earlyExitFlag() {
        auto r = parseArgs({"shot.png", "--early-exit"});
        QVERIFY(r.ok);
        QVERIFY(r.options.earlyExit);
    }
    void missingInputIsError() {
        auto r = parseArgs({});
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }
    void unknownFlagIsError() {
        auto r = parseArgs({"--frobnicate"});
        QVERIFY(!r.ok);
    }
};

QTEST_GUILESS_MAIN(TestCli)
#include "test_cli.moc"

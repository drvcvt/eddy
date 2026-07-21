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
    void preservesUnicodeFilePath() {
        const QString path = QStringLiteral("C:/Bilder/Grüße/截图.png");
        const auto r = parseArgs({path});
        QVERIFY(r.ok);
        QCOMPARE(r.options.input.path, path);
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
        QVERIFY(r.options.output.copyFlagSet);
    }
    void copyFlagUnsetByDefault() {
        auto r = parseArgs({"shot.png"});
        QVERIFY(r.ok);
        QVERIFY(!r.options.output.copyFlagSet);
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
    void parsesNoAnim() {
        auto r = parseArgs({QStringLiteral("img.png"), QStringLiteral("--no-anim")});
        QVERIFY(r.ok);
        QVERIFY(r.options.noAnim);
    }
    void parsesBoltsnapCardId() {
        auto r = parseArgs({QStringLiteral("img.png"), QStringLiteral("--boltsnap-card-id"),
                            QStringLiteral("184467")});
        QVERIFY(r.ok);
        QCOMPARE(r.options.boltsnapCardId, quint64(184467));
    }
    void rejectsInvalidBoltsnapCardId() {
        auto r = parseArgs({QStringLiteral("img.png"), QStringLiteral("--boltsnap-card-id"),
                            QStringLiteral("nope")});
        QVERIFY(!r.ok);
    }
    void rejectsBoltsnapCardIdOutsideJsonIntegerRange() {
        auto r = parseArgs({QStringLiteral("img.png"), QStringLiteral("--boltsnap-card-id"),
                            QStringLiteral("9223372036854775808")});
        QVERIFY(!r.ok);
    }
};

QTEST_GUILESS_MAIN(TestCli)
#include "test_cli.moc"

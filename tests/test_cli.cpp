#include <QtTest>
#include "cli.h"

class TestCli : public QObject {
    Q_OBJECT
private slots:
    void version_isNonEmpty() {
        QVERIFY(!eddy::versionString().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCli)
#include "test_cli.moc"

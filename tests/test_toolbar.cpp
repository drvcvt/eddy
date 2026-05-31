#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include "toolbar.h"
using namespace eddy;
class TestToolbar : public QObject {
    Q_OBJECT
private slots:
    void emitsToolChosen() {
        Toolbar tb;
        QSignalSpy spy(&tb, &Toolbar::toolChosen);
        auto buttons = tb.findChildren<QToolButton*>();
        QVERIFY(!buttons.isEmpty());
        buttons[1]->click(); // Arrow (index 0 = Move)
        QCOMPARE(spy.count(), 1);
    }
};
QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"

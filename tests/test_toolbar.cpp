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
        buttons[1]->click();              // index 0 = Move, 1 = Arrow
        QCOMPARE(spy.count(), 1);
    }
    void syncToolChecksButtonAndPlacesPill() {
        Toolbar tb;
        tb.setAnimationsEnabled(false);
        tb.resize(700, 46);
        tb.show();                        // lay out so geometries are real
        QVERIFY(QTest::qWaitForWindowExposed(&tb));
        tb.syncTool(ToolType::Rect);
        auto *pill = tb.findChild<QWidget*>("Pill");
        QVERIFY(pill);
        QToolButton *rectBtn = nullptr;
        for (auto *b : tb.findChildren<QToolButton*>())
            if (b->isChecked() && !b->objectName().startsWith("Width")) rectBtn = b;  // exclude the width chooser (M is default-checked)
        QVERIFY(rectBtn);
        QCOMPARE(pill->geometry(), rectBtn->geometry());
    }
    void widthButtonsEmit() {
        Toolbar tb;
        QSignalSpy spy(&tb, &Toolbar::widthChosen);
        auto *L = tb.findChild<QToolButton*>("WidthL");
        QVERIFY(L);
        L->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 8.0);
    }
};
QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"

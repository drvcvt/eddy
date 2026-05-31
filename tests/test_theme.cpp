#include <QtTest>
#include <QPalette>
#include "theme.h"
using namespace eddy;
class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void paletteIsDark() {
        QPalette p = theme::darkPalette();
        QVERIFY(p.color(QPalette::Window).lightnessF() < 0.2);
        QVERIFY(p.color(QPalette::Base).lightnessF()   < 0.2);
        QCOMPARE(p.color(QPalette::Highlight), QColor(theme::kAccent));
    }
};
QTEST_MAIN(TestTheme)
#include "test_theme.moc"

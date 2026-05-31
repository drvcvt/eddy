#include <QtTest>
#include <QPalette>
#include <QIcon>
#include <QPixmap>
#include <QImage>
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
    void tintedIconRecolours() {
        QIcon ic = theme::tintedIcon(":/icons/rect.svg",
                                     QColor(theme::kIconRest), QColor(theme::kIconActive));
        QVERIFY(!ic.isNull());
        QPixmap off = ic.pixmap(QSize(22,22), QIcon::Normal, QIcon::Off);
        QPixmap on  = ic.pixmap(QSize(22,22), QIcon::Normal, QIcon::On);
        QVERIFY(!off.isNull());
        QVERIFY(!on.isNull());
        QVERIFY(off.toImage() != on.toImage());   // rest vs active colour differ
    }
};
QTEST_MAIN(TestTheme)
#include "test_theme.moc"

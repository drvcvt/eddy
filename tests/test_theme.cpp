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
        QCOMPARE(p.color(QPalette::Window), QColor("#121212"));
        QCOMPARE(p.color(QPalette::Base), QColor("#1A1A1A"));
        QCOMPARE(p.color(QPalette::WindowText), QColor("#ECECEC"));
        QCOMPARE(p.color(QPalette::PlaceholderText), QColor("#5C5C5C"));
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
    void lightPaletteUsesApprovedTokens() {
        const QPalette p = theme::palette(false);
        QCOMPARE(p.color(QPalette::Window), QColor("#FAFAFA"));
        QCOMPARE(p.color(QPalette::Base), QColor("#F1F1F1"));
        QCOMPARE(p.color(QPalette::WindowText), QColor("#1A1A1A"));
        QCOMPARE(p.color(QPalette::Highlight), QColor("#1A1A1A"));
        QCOMPARE(p.color(QPalette::HighlightedText), QColor("#FAFAFA"));
    }
    void systemThemeResolvesFromWindowBrightness() {
        QPalette light;
        light.setColor(QPalette::Window, QColor("#fafafa"));
        QPalette dark;
        dark.setColor(QPalette::Window, QColor("#121212"));
        QVERIFY(!theme::resolveDark(ThemeMode::System, light));
        QVERIFY(theme::resolveDark(ThemeMode::System, dark));
        QVERIFY(theme::resolveDark(ThemeMode::Dark, light));
        QVERIFY(!theme::resolveDark(ThemeMode::Light, dark));
    }
    void styleSheetExpandsSemanticTokens() {
        const QString dark = theme::styleSheet(true);
        const QString light = theme::styleSheet(false);
        QVERIFY(!dark.isEmpty());
        QVERIFY(!light.isEmpty());
        QVERIFY(!dark.contains(QStringLiteral("@bg")));
        QVERIFY(!light.contains(QStringLiteral("@bg")));
        QVERIFY(dark.contains(QStringLiteral("#121212")));
        QVERIFY(light.contains(QStringLiteral("#FAFAFA")));
        QVERIFY(dark.contains(QStringLiteral("QToolTip")));
        QVERIFY(light.contains(QStringLiteral("QToolTip")));
        QVERIFY(dark.contains(QStringLiteral("QLabel#TrimInTime")));
        QVERIFY(dark.contains(QStringLiteral("QLabel#TrimOutTime")));
        QVERIFY(!dark.contains(QStringLiteral("Z003")));
        QVERIFY(dark.contains(QStringLiteral("font-family: \"Geist\"")));
    }
};
QTEST_MAIN(TestTheme)
#include "test_theme.moc"

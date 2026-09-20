#include <QtTest>
#include <QPalette>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QDirIterator>
#include <QSvgRenderer>
#include "theme.h"
using namespace eddy;
class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void paletteIsDark() {
        QPalette p = theme::darkPalette();
        QCOMPARE(p.color(QPalette::Window), QColor("#181818"));
        QCOMPARE(p.color(QPalette::Base), QColor("#202020"));
        QCOMPARE(p.color(QPalette::WindowText), QColor("#EEEEEE"));
        QCOMPARE(p.color(QPalette::PlaceholderText), QColor("#999999"));
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
    void tintedIconRendersAtTheRequestedSize() {
        // Rendering at a fixed size and displaying at another resamples every
        // stroke, which is what made the 18px chrome look soft.
        for (int size : {theme::kIconSize, 24}) {
            const QIcon icon = theme::tintedIcon(":/icons/rect.svg",
                                                 QColor(theme::kIconRest),
                                                 QColor(theme::kIconActive), size);
            // availableSizes() reports what was actually rasterised, so a
            // stale fixed render size shows up here even though pixmap() would
            // happily rescale it.
            QCOMPARE(icon.availableSizes(), QList<QSize>{QSize(size * 2, size * 2)});
            const QPixmap pm = icon.pixmap(QSize(size, size), 2.0);
            QCOMPARE(pm.devicePixelRatio(), 2.0);
            QCOMPARE(pm.size(), QSize(size * 2, size * 2));
        }
    }
    void everyIconSharesOneGrid() {
        // One keyline for the whole set: ink centred in the 24-unit viewBox and
        // reaching a 20-unit live area. Drawing each icon to its own bounds is
        // what made rows of them look ragged.
        QDirIterator icons(QStringLiteral(":/icons"), {QStringLiteral("*.svg")}, QDir::Files);
        int checked = 0;
        while (icons.hasNext()) {
            const QString path = icons.next();
            QSvgRenderer renderer(path);
            QVERIFY2(renderer.isValid(), qPrintable(path));
            constexpr int kPixels = 240;                 // 10x the viewBox
            constexpr qreal kUnit = kPixels / 24.0;
            QImage sheet(kPixels, kPixels, QImage::Format_ARGB32_Premultiplied);
            sheet.fill(Qt::transparent);
            QPainter painter(&sheet);
            renderer.render(&painter, QRectF(0, 0, kPixels, kPixels));
            painter.end();
            int left = kPixels, top = kPixels, right = -1, bottom = -1;
            for (int y = 0; y < kPixels; ++y)
                for (int x = 0; x < kPixels; ++x)
                    if (qAlpha(sheet.pixel(x, y))) {
                        left = qMin(left, x); right = qMax(right, x);
                        top = qMin(top, y); bottom = qMax(bottom, y);
                    }
            QVERIFY2(right >= 0, qPrintable(path + " draws nothing"));
            const qreal width = (right - left + 1) / kUnit;
            const qreal height = (bottom - top + 1) / kUnit;
            const qreal cx = (left + right + 1) / 2.0 / kUnit;
            const qreal cy = (top + bottom + 1) / 2.0 / kUnit;
            QVERIFY2(qAbs(cx - 12.0) <= 0.2, qPrintable(QStringLiteral("%1 cx=%2").arg(path).arg(cx)));
            QVERIFY2(qAbs(cy - 12.0) <= 0.2, qPrintable(QStringLiteral("%1 cy=%2").arg(path).arg(cy)));
            QVERIFY2(qAbs(qMax(width, height) - 20.0) <= 0.3,
                     qPrintable(QStringLiteral("%1 live=%2").arg(path).arg(qMax(width, height))));
            ++checked;
        }
        QVERIFY2(checked >= 25, qPrintable(QStringLiteral("only %1 icons found").arg(checked)));
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
        dark.setColor(QPalette::Window, QColor("#181818"));
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
        QVERIFY(dark.contains(QStringLiteral("#181818")));
        QVERIFY(light.contains(QStringLiteral("#FAFAFA")));
        QVERIFY(dark.contains(QStringLiteral("QToolTip")));
        QVERIFY(light.contains(QStringLiteral("QToolTip")));
        QVERIFY(dark.contains(QStringLiteral("QWidget#PlaybackBar QLabel")));
        QVERIFY(!dark.contains(QStringLiteral("Z003")));
        QVERIFY(dark.contains(QStringLiteral("font-family: \"Noto Sans\"")));
    }
};
QTEST_MAIN(TestTheme)
#include "test_theme.moc"

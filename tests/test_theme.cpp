#include <QtTest>
#include <QPalette>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QDirIterator>
#include <QSvgRenderer>
#include <QFile>
#include <QFontDatabase>
#include <QRegularExpression>
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
    void tintedIconRendersAtTheDevicePixelSize() {
        // A fixed 2x pixmap gets smooth-scaled on 1x and fractional displays,
        // which made the chrome icons softer than the text beside them.
        const QIcon icon = theme::tintedIcon(":/icons/rect.svg", QColor(theme::kIconRest),
                                             QColor(theme::kIconActive));
        QVERIFY(icon.availableSizes().isEmpty());          // nothing pre-rasterised
        for (qreal scale : {1.0, 1.5, 2.0}) {
            const int size = theme::kIconSize;
            const QPixmap pm = icon.pixmap(QSize(size, size), scale);
            QCOMPARE(pm.devicePixelRatio(), scale);
            QCOMPARE(pm.size(), QSize(qRound(size * scale), qRound(size * scale)));
        }
        QVERIFY(theme::tintedIcon(":/icons/missing.svg", Qt::white, Qt::white).isNull());
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
    void typeComesOnlyFromTheScale() {
        // Raw sizes and families crept in one widget at a time (9 to 14px, three
        // faces); the sheet may only name the font and size tokens.
        QFile file(QStringLiteral(":/eddy.qss"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString raw = QString::fromUtf8(file.readAll());
        static const QRegularExpression rawType(
            QStringLiteral("font-(size|family)\\s*:\\s*+(?!@f)[^;]*"));
        const auto hit = rawType.match(raw);
        QVERIFY2(!hit.hasMatch(), qPrintable(hit.captured()));
        const QString qss = theme::styleSheet(true);
        QVERIFY(!qss.contains(QStringLiteral("@f")));
        QVERIFY(qss.contains(QStringLiteral("\"Outfit\"")));
        QVERIFY(QFontDatabase::families().contains(QStringLiteral("Outfit")));
    }
    void sheetKeepsToTheSpacingScaleAndDrawsNoBorders() {
        QFile file(QStringLiteral(":/eddy.qss"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString raw = QString::fromUtf8(file.readAll());
        static const QSet<int> scale{0, 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 72};
        static const QRegularExpression spacing(QStringLiteral("(padding|margin)[a-z-]*\\s*:([^;]*);"));
        static const QRegularExpression number(QStringLiteral("-?(\\d+)"));
        for (auto rule = spacing.globalMatch(raw); rule.hasNext();) {
            const auto declaration = rule.next();
            for (auto value = number.globalMatch(declaration.captured(2)); value.hasNext();)
                QVERIFY2(scale.contains(value.next().captured(1).toInt()),
                         qPrintable(declaration.captured()));
        }
        // Depth is brightness only: no outline may frame an element.
        static const QRegularExpression border(QStringLiteral("border\\s*:\\s*+(?!none)[^;]*"));
        const auto framed = border.match(raw);
        QVERIFY2(!framed.hasMatch(), qPrintable(framed.captured()));
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
        QVERIFY(dark.contains(QStringLiteral("font-family: \"Outfit\"")));
    }
};
QTEST_MAIN(TestTheme)
#include "test_theme.moc"

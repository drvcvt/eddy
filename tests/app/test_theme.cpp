#include <QtTest>
#include <cmath>
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
#include "app/theme.h"
using namespace eddy;
class TestTheme : public QObject {
    Q_OBJECT
private slots:
    void paletteIsDark() {
        QPalette p = theme::darkPalette();
        QCOMPARE(p.color(QPalette::Window), theme::tone(theme::Tone::Bg, true));
        QCOMPARE(p.color(QPalette::Base), theme::tone(theme::Tone::Raise1, true));
        QCOMPARE(p.color(QPalette::WindowText), theme::tone(theme::Tone::Fg, true));
        QCOMPARE(p.color(QPalette::PlaceholderText), theme::tone(theme::Tone::Sub, true));
        QCOMPARE(p.color(QPalette::Highlight), theme::tone(theme::Tone::Raise3, true));
        QVERIFY(p.color(QPalette::Window).lightnessF() < 0.15);
    }
    void graphiteLeansCool() {
        // One hue for every surface: a touch of blue, never a tint you would name.
        for (bool dark : {true, false}) {
            const QColor bg = theme::tone(theme::Tone::Bg, dark);
            QVERIFY2(bg.blue() > bg.red(), qPrintable(bg.name()));
            QVERIFY2(bg.blue() - bg.red() <= 6, qPrintable(bg.name()));
        }
    }
    void textKeepsItsContrastOnEverySurface() {
        // WCAG relative luminance; body text needs 4.5:1 wherever it sits.
        const auto luminance = [](const QColor &c) {
            auto channel = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
        };
        const auto contrast = [&](const QColor &a, const QColor &b) {
            const qreal la = luminance(a), lb = luminance(b);
            return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
        };
        using theme::Tone;
        const QList<QPair<Tone, QList<Tone>>> pairs{
            {Tone::Fg, {Tone::Bg, Tone::Raise1, Tone::Raise2, Tone::Raise3, Tone::SegmentOn, Tone::BarActive}},
            {Tone::Sub, {Tone::Bg, Tone::Raise1, Tone::BarHover}},
        };
        QStringList low;
        for (bool dark : {true, false})
            for (const auto &[text, surfaces] : pairs)
                for (Tone surface : surfaces) {
                    const qreal ratio = contrast(theme::tone(text, dark), theme::tone(surface, dark));
                    if (ratio < 4.5)
                        low << QStringLiteral("%1 %2 on %3: %4").arg(dark ? "dark" : "light")
                                   .arg(int(text)).arg(int(surface)).arg(ratio, 0, 'f', 2);
                }
        QVERIFY2(low.isEmpty(), qPrintable(low.join(QStringLiteral(", "))));
    }
    void tintedIconRecolours() {
        QIcon ic = theme::tintedIcon(":/icons/rect.svg",
                                     theme::tone(theme::Tone::Sub, true), theme::tone(theme::Tone::Fg, true));
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
        const QIcon icon = theme::tintedIcon(":/icons/rect.svg", theme::tone(theme::Tone::Sub, true),
                                             theme::tone(theme::Tone::Fg, true));
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
        QCOMPARE(p.color(QPalette::Window), theme::tone(theme::Tone::Bg, false));
        QCOMPARE(p.color(QPalette::Base), theme::tone(theme::Tone::Raise1, false));
        QCOMPARE(p.color(QPalette::WindowText), theme::tone(theme::Tone::Fg, false));
        QCOMPARE(p.color(QPalette::Highlight), theme::tone(theme::Tone::Fg, false));
        QCOMPARE(p.color(QPalette::HighlightedText), theme::tone(theme::Tone::Bg, false));
        QVERIFY(p.color(QPalette::Window).lightnessF() > 0.95);
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
        // Raw numbers only: a token such as @s3 is on the scale by definition.
        static const QRegularExpression number(QStringLiteral("(?<![@\\w-])-?(\\d+)"));
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
        QVERIFY(dark.contains(theme::tone(theme::Tone::Bg, true).name()));
        QVERIFY(light.contains(theme::tone(theme::Tone::Bg, false).name()));
        // Every token expanded: colours, sizes and radii alike.
        static const QRegularExpression leftover(QStringLiteral("@[a-z]"));
        QVERIFY2(!leftover.match(dark).hasMatch(), qPrintable(leftover.match(dark).captured()));
        QVERIFY(dark.contains(QStringLiteral("QToolTip")));
        QVERIFY(light.contains(QStringLiteral("QToolTip")));
        QVERIFY(dark.contains(QStringLiteral("QWidget#PlaybackBar QLabel")));
        QVERIFY(!dark.contains(QStringLiteral("Z003")));
        QVERIFY(dark.contains(QStringLiteral("font-family: \"Outfit\"")));
    }
};
QTEST_MAIN(TestTheme)
#include "test_theme.moc"

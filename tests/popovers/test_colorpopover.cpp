#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include <QLineEdit>
#include "popovers/colorpopover.h"
#include "app/theme.h"
#include <QApplication>
#include <cmath>
using namespace eddy;
class TestColorPopover : public QObject {
    Q_OBJECT
private slots:
    void currentColourAndHexEntry() {
        ColorPopover p(nullptr, QColor("#0a84ff"));
        auto *hex = p.findChild<QLineEdit*>("ColorHex");
        QVERIFY(hex);
        QCOMPARE(hex->text(), QString("#0A84FF"));
        int checked = 0;
        for (auto *b : p.findChildren<QToolButton*>()) {
            if (b->isChecked()) {
                ++checked;
                QCOMPARE(b->accessibleName(), QString("#0A84FF"));
            }
        }
        QCOMPARE(checked, 1);
        QSignalSpy spy(&p, &ColorPopover::picked);
        hex->setText("#12");
        QTest::keyClick(hex, Qt::Key_Return);
        QCOMPARE(spy.count(), 0);
        hex->setText("#AbCdEf");
        QTest::keyClick(hex, Qt::Key_Return);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<QColor>(spy.at(0).at(0)), QColor("#abcdef"));
    }
    void customDialogStartsWithCurrentColour() {
        ColorPopover p(nullptr, QColor("#123456"));
        auto *custom = p.findChild<QToolButton*>("Custom");
        QVERIFY(custom);
        QSignalSpy spy(&p, &ColorPopover::customRequested);
        custom->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<QColor>(spy.at(0).at(0)), QColor("#123456"));
    }
    void presetsEmitColour() {
        ColorPopover p;
        auto btns = p.findChildren<QToolButton*>();
        QVERIFY(btns.size() >= 9);            // 8 presets + Custom
        QSignalSpy spy(&p, &ColorPopover::picked);
        btns[0]->click();                     // first preset = #ff3b30
        QCOMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<QColor>(spy.at(0).at(0)), QColor("#ff3b30"));
    }
    void pipetteRequestsEyedropper() {
        ColorPopover p;
        QToolButton *pipette = nullptr;
        for (auto *b : p.findChildren<QToolButton*>())
            if (b->objectName() == "Pipette") pipette = b;
        QVERIFY(pipette);
        QSignalSpy spy(&p, &ColorPopover::eyedropperRequested);
        pipette->click();
        QCOMPARE(spy.count(), 1);
    }
    void roundCornersAndRingedSwatches() {
        // The popover's corners are the stylesheet's radius, not a square window; the
        // near-black preset keeps a visible edge on the popover's own grey.
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
        ColorPopover p(nullptr, QColor("#0a84ff"));
        p.show();
        QVERIFY(QTest::qWaitForWindowExposed(&p));
        const QImage shot = p.grab().toImage();
        QCOMPARE(shot.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(shot.pixelColor(shot.width() / 2, shot.height() / 2).alpha(), 255);
        QToolButton *dark = nullptr;
        for (auto *b : p.findChildren<QToolButton *>())
            if (b->accessibleName() == QStringLiteral("#1A1A1A")) dark = b;
        QVERIFY(dark);
        const QImage swatch = dark->grab().toImage();
        const QColor surface = swatch.pixelColor(0, swatch.height() / 2);
        const int edge = (swatch.width() - theme::kFloatIcon) / 2 + 1;   // the ring's column
        const auto luminance = [](QColor c) {
            auto ch = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
        };
        qreal best = 0;
        for (int x = edge - 1; x <= edge + 1; ++x) {
            const qreal a = luminance(swatch.pixelColor(x, swatch.height() / 2)), b = luminance(surface);
            best = std::max(best, (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05));
        }
        QVERIFY2(best >= 3, qPrintable(QString::number(best)));
        qApp->setStyleSheet({});
    }
};
QTEST_MAIN(TestColorPopover)
#include "test_colorpopover.moc"

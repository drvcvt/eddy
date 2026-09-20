#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include <QLineEdit>
#include "colorpopover.h"
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
};
QTEST_MAIN(TestColorPopover)
#include "test_colorpopover.moc"

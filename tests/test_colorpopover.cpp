#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include "colorpopover.h"
using namespace eddy;
class TestColorPopover : public QObject {
    Q_OBJECT
private slots:
    void presetsEmitColour() {
        ColorPopover p;
        auto btns = p.findChildren<QToolButton*>();
        QVERIFY(btns.size() >= 9);            // 8 presets + Custom
        QSignalSpy spy(&p, &ColorPopover::picked);
        btns[0]->click();                     // first preset = #ff3b30
        QCOMPARE(spy.count(), 1);
        QCOMPARE(qvariant_cast<QColor>(spy.at(0).at(0)), QColor("#ff3b30"));
    }
};
QTEST_MAIN(TestColorPopover)
#include "test_colorpopover.moc"

#include <QtTest>
#include <QToolButton>
#include "redactbar.h"
#include "items/redactitem.h"

using namespace eddy;

class TestRedactBar : public QObject {
    Q_OBJECT
private slots:
    void hasFourModeButtons() {
        RedactBar bar;
        QCOMPARE(bar.findChildren<QToolButton *>().size(), 4);
    }
    void clickEmitsModeChosen() {
        RedactBar bar;
        RedactMode got = RedactMode::Blur;
        int count = 0;
        QObject::connect(&bar, &RedactBar::modeChosen, &bar,
                         [&](RedactMode m) { got = m; ++count; });
        for (auto *b : bar.findChildren<QToolButton *>())
            if (b->text() == "OCR Black") { b->click(); break; }
        QCOMPARE(count, 1);
        QVERIFY(got == RedactMode::OcrBlacken);
    }
    void setModeChecksButton() {
        RedactBar bar;
        bar.setMode(RedactMode::OcrBlur);
        bool found = false;
        for (auto *b : bar.findChildren<QToolButton *>())
            if (b->text() == "OCR Blur") { QVERIFY(b->isChecked()); found = true; }
        QVERIFY(found);
    }
};

QTEST_MAIN(TestRedactBar)
#include "test_redactbar.moc"

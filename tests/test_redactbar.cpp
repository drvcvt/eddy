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
        const auto scopes = bar.findChildren<QToolButton *>(QStringLiteral("TimeScope"));
        QCOMPARE(bar.findChildren<QToolButton *>().size() - scopes.size(), 4);
        // Whole clip and From playhead appear for videos only.
        QCOMPARE(scopes.size(), 2);
        for (auto *b : scopes) QVERIFY(b->isHidden());
        bar.setTimeScope(true, true);
        for (auto *b : scopes) QVERIFY(!b->isHidden());
        QVERIFY(scopes[1]->isChecked());
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

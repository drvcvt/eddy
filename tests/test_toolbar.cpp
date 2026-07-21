#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include "toolbar.h"
using namespace eddy;
class TestToolbar : public QObject {
    Q_OBJECT
private slots:
    void emitsToolChosen() {
        Toolbar tb;
        QSignalSpy spy(&tb, &Toolbar::toolChosen);
        QList<QToolButton*> tools;
        for (auto *b : tb.findChildren<QToolButton*>())
            if (b->isCheckable() && !b->objectName().startsWith("Width")) tools << b;
        QVERIFY(tools.size() >= 2);
        tools[1]->click();              // tools are Move(0), Arrow(1), ...
        QCOMPARE(spy.count(), 1);
    }
    void undoRedoButtonsEmit() {
        Toolbar tb;
        QSignalSpy us(&tb, &Toolbar::undoRequested);
        QSignalSpy rs(&tb, &Toolbar::redoRequested);
        auto *u = tb.findChild<QToolButton*>("Undo");
        auto *r = tb.findChild<QToolButton*>("Redo");
        QVERIFY(u); QVERIFY(r);
        // Buttons start disabled (empty stack); enable for signal emission test.
        tb.setUndoEnabled(true); tb.setRedoEnabled(true);
        u->click(); r->click();
        QCOMPARE(us.count(), 1);
        QCOMPARE(rs.count(), 1);
    }
    void syncToolUsesCheckedStateWithoutOverlay() {
        Toolbar tb;
        tb.resize(700, 46);
        tb.show();                        // lay out so geometries are real
        QVERIFY(QTest::qWaitForWindowExposed(&tb));
        tb.syncTool(ToolType::Rect);
        auto *pill = tb.findChild<QWidget*>("Pill");
        QToolButton *rectBtn = nullptr;
        for (auto *b : tb.findChildren<QToolButton*>())
            if (b->isChecked() && !b->objectName().startsWith("Width")) rectBtn = b;  // exclude the width chooser (M is default-checked)
        QVERIFY(rectBtn);
        QVERIFY(rectBtn->isChecked());
        QVERIFY(pill == nullptr);
    }
    void swatchShowsPaintedDisc() {
        Toolbar tb;
        auto *swatch = tb.findChild<QToolButton*>("Swatch");
        QVERIFY(swatch);
        QVERIFY(!swatch->icon().isNull());        // painted colour disc, not an empty glyph
        tb.setSwatchColor(QColor("#0a84ff"));
        QVERIFY(!swatch->icon().isNull());
    }
    void widthButtonsEmit() {
        Toolbar tb;
        QSignalSpy spy(&tb, &Toolbar::widthChosen);
        auto *L = tb.findChild<QToolButton*>("WidthL");
        QVERIFY(L);
        for (const char *name : {"WidthS", "WidthM", "WidthL"}) {
            auto *button = tb.findChild<QToolButton *>(name);
            QVERIFY(button);
            QVERIFY(button->text().isEmpty());
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->toolTip().isEmpty());
            QCOMPARE(button->accessibleName(), button->toolTip());
        }
        L->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 8.0);
    }
    void sendToShelfButtonEmits() {
        Toolbar tb;
        QSignalSpy spy(&tb, &Toolbar::sendToShelfRequested);
        auto *shelf = tb.findChild<QToolButton*>("SendToShelf");
        QVERIFY(shelf);
        QVERIFY(!shelf->icon().isNull());
        shelf->click();
        QCOMPARE(spy.count(), 1);
    }
    void themeActionIsATrailingIcon() {
        Toolbar tb;
        tb.resize(900, 46);
        tb.show();
        QVERIFY(QTest::qWaitForWindowExposed(&tb));
        auto *theme = tb.findChild<QToolButton *>("Theme");
        auto *shelf = tb.findChild<QToolButton *>("SendToShelf");
        QVERIFY(theme && shelf);
        QVERIFY(theme->x() > shelf->x());
        QVERIFY(theme->text().isEmpty());
        QVERIFY(!theme->icon().isNull());
        QCOMPARE(theme->accessibleName(), theme->toolTip());

        tb.setDark(true);
        const QImage darkAction = theme->icon().pixmap(20, 20).toImage();
        tb.setDark(false);
        QVERIFY(darkAction != theme->icon().pixmap(20, 20).toImage());
    }
    void compactModeKeepsCoreToolsAndHidesShortcutDuplicates() {
        Toolbar tb;
        tb.setCompact(true);
        for (const char *name : {"Undo", "Redo", "WidthS", "WidthM", "WidthL", "Save", "Copy"}) {
            auto *button = tb.findChild<QToolButton *>(name);
            QVERIFY(button);
            QVERIFY2(button->isHidden(), name);
        }
        for (const char *name : {"move", "text", "spotlight", "Swatch", "SendToShelf", "Theme"}) {
            auto *button = tb.findChild<QToolButton *>(name);
            QVERIFY(button);
            QVERIFY2(!button->isHidden(), name);
        }
        tb.setCompact(false);
        QVERIFY(!tb.findChild<QToolButton *>("Save")->isHidden());
        QVERIFY(!tb.findChild<QToolButton *>("WidthM")->isHidden());
    }
};
QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"

#include <QtTest>
#include <QSignalSpy>
#include <QToolButton>
#include <QMenu>
#include <QApplication>
#include <QScopeGuard>
#include "theme.h"
#include "toolbar.h"
using namespace eddy;
class TestToolbar : public QObject {
    Q_OBJECT
private slots:
    void frameCopyHasSeparateActionAndKeepsNormalCopy() {
        Toolbar toolbar;
        toolbar.enableVideoFrameCopy();
        auto *copy = toolbar.findChild<QToolButton *>("Copy");
        QSignalSpy video(&toolbar, &Toolbar::copyRequested);
        QSignalSpy frame(&toolbar, &Toolbar::copyFrameRequested);
        copy->click();
        QCOMPARE(video.count(), 1);
        QCOMPARE(frame.count(), 0);
        auto *action = toolbar.findChild<QAction *>("CopyVideoFrame");
        QVERIFY(action);
        action->trigger();
        QCOMPARE(frame.count(), 1);
        QCOMPARE(video.count(), 1);
    }
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
    void undoRedoUseIconsNotFontGlyphs() {
        Toolbar tb;
        for (const char *name : {"Undo", "Redo"}) {
            auto *button = tb.findChild<QToolButton *>(name);
            QVERIFY(button);
            QVERIFY(button->text().isEmpty());
            QVERIFY(!button->icon().isNull());
        }
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
    void labelledButtonsCentreTheirIconOnTheCapitals() {
        // Measured on the rendered pixels (mt-ui-style): the glyph's ink sits on
        // the middle of the capitals, 8 px from the label, 8 px from the edges.
        qApp->setStyleSheet(theme::styleSheet(true));
        const auto restoreSheet = qScopeGuard([] { qApp->setStyleSheet({}); });
        Toolbar bar;
        bar.resize(900, 30);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        for (const char *name : {"Studio", "SendToShelf"}) {
            auto *button = bar.findChild<QToolButton *>(QLatin1String(name));
            const QImage image = bar.grab(button->geometry()).toImage().convertToFormat(QImage::Format_RGB32);
            const int background = qGray(image.pixel(1, 1));
            auto ink = [&](int x, int y) { return qAbs(qGray(image.pixel(x, y)) - background) > 60; };
            // Ink columns in runs: the glyph first, then the first capital.
            QList<QPair<int, int>> runs;
            int start = -1;
            for (int x = 0; x <= image.width(); ++x) {
                bool any = false;
                for (int y = 0; x < image.width() && y < image.height(); ++y) any = any || ink(x, y);
                if (any && start < 0) start = x;
                if (!any && start >= 0) { runs.append({start, x - 1}); start = -1; }
            }
            QVERIFY2(runs.size() >= 3, name);
            auto rows = [&](QPair<int, int> run) {
                int top = image.height(), bottom = -1;
                for (int x = run.first; x <= run.second; ++x)
                    for (int y = 0; y < image.height(); ++y)
                        if (ink(x, y)) { top = qMin(top, y); bottom = qMax(bottom, y); }
                return (top + bottom) / 2.0;
            };
            const QString where = QString::fromLatin1(name);
            QVERIFY2(qAbs(rows(runs[0]) - rows(runs[1])) <= 0.5,
                     qPrintable(where + QStringLiteral(": glyph %1, capital %2").arg(rows(runs[0])).arg(rows(runs[1]))));
            QVERIFY2(qAbs(runs[1].first - runs[0].second - 1 - 8) <= 1,
                     qPrintable(where + QStringLiteral(": gap %1").arg(runs[1].first - runs[0].second - 1)));
            QCOMPARE(runs.first().first, 8);
            QVERIFY2(qAbs(image.width() - 1 - runs.last().second - 8) <= 1,
                     qPrintable(where + QStringLiteral(": right edge %1").arg(image.width() - 1 - runs.last().second)));
        }
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

};
QTEST_MAIN(TestToolbar)
#include "test_toolbar.moc"

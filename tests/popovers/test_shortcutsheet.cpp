// H1: every shortcut on one sheet, opened by ?, F1 or the keyboard button; H2: the
// right-click buttons carry the corner mark, nothing else does.
#include "../editor/editortest.h"
#include "popovers/shortcutsheet.h"

namespace {

std::unique_ptr<EditorWindow> open(bool video, QSize size) {
    MediaDocument doc;
    if (video) {
        doc.kind = MediaKind::Video;
        doc.path = QStringLiteral("/tmp/nonexistent-shortcut-sheet.mp4");
        doc.video = {QSize(64, 48), 4000, 25.0};
    } else {
        doc.kind = MediaKind::Image;
        doc.image = QImage(64, 48, QImage::Format_ARGB32);
        doc.image.fill(Qt::gray);
    }
    Config cfg; cfg.animations = false;
    auto window = std::make_unique<EditorWindow>(doc, cfg, CliOptions());
    window->resize(size);
    window->show();
    return window;
}

ShortcutSheet *sheetOf(const EditorWindow &w) {
    for (QWidget *top : QApplication::topLevelWidgets())
        if (auto *sheet = qobject_cast<ShortcutSheet *>(top); sheet && sheet->parentWidget() == &w && sheet->isVisible())
            return sheet;
    return nullptr;
}

// A closed sheet deletes itself later; until then the editor still holds it (a second
// click on the button closes rather than reopens, as with Studio).
void settle() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }

QStringList titlesOf(const ShortcutSheet *sheet) {
    QStringList titles;
    for (auto *label : sheet->findChildren<QLabel *>(QStringLiteral("ShortcutGroup"))) titles << label->text();
    return titles;
}

}

class TestShortcutSheet : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
    }

    void questionMarkF1AndTheButtonOpenIt() {
        auto w = open(true, QSize(1000, 680));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::keyClick(w.get(), Qt::Key_Question, Qt::ShiftModifier);
        QVERIFY(sheetOf(*w));
        QTest::keyClick(sheetOf(*w), Qt::Key_Escape);
        QTRY_VERIFY(!sheetOf(*w));
        settle();
        QTest::keyClick(w.get(), Qt::Key_F1);
        QVERIFY(sheetOf(*w));
        QTest::keyClick(sheetOf(*w), Qt::Key_F1);   // the same key closes it again
        QTRY_VERIFY(!sheetOf(*w));
        settle();
        auto *button = w->findChild<QToolButton *>(QStringLiteral("Shortcuts"));
        QVERIFY(button);
        QCOMPARE(button->accessibleName(), QStringLiteral("Shortcuts"));
        button->click();
        QVERIFY(sheetOf(*w));
        sheetOf(*w)->close();
    }

    void anImageListsNoVideoKeys() {
        auto w = open(false, QSize(1000, 680));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::keyClick(w.get(), Qt::Key_F1);
        QVERIFY(sheetOf(*w));
        QVERIFY(!titlesOf(sheetOf(*w)).contains(QStringLiteral("Video and timeline")));
        sheetOf(*w)->close();
        auto v = open(true, QSize(1000, 680));
        QTest::keyClick(v.get(), Qt::Key_F1);
        QVERIFY(titlesOf(sheetOf(*v)).contains(QStringLiteral("Video and timeline")));
        sheetOf(*v)->close();
    }

    void itFitsTheSmallestWindow() {
        // 760 x 600: the columns stack and the sheet scrolls, inside the window, under the bar.
        auto w = open(true, QSize(760, 600));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QTest::keyClick(w.get(), Qt::Key_F1);
        ShortcutSheet *sheet = sheetOf(*w);
        QVERIFY(sheet);
        const QRect window(w->mapToGlobal(QPoint(0, 0)), w->size());
        QVERIFY2(window.contains(sheet->geometry()),
                 qPrintable(QStringLiteral("sheet %1,%2 %3x%4").arg(sheet->x()).arg(sheet->y())
                                .arg(sheet->width()).arg(sheet->height())));
        for (auto *label : sheet->findChildren<QLabel *>())
            QVERIFY2(label->width() >= label->sizeHint().width(), qPrintable(label->text()));
        sheet->close();
    }

    void theToolKeysAreTheToolbars() {
        // Each listed tool key is the one its toolbar button names, and pressing it picks that tool.
        auto w = open(false, QSize(1000, 680));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        const auto &tools = ShortcutSheet::groups().first();
        QCOMPARE(tools.title, QStringLiteral("Tools"));
        for (const auto &row : tools.rows) {
            QToolButton *button = nullptr;
            for (auto *b : w->findChildren<QToolButton *>())
                if (b->isCheckable() && b->toolTip() == row.what + QLatin1Char('\t') + row.keys) button = b;
            QVERIFY2(button, qPrintable(row.keys + QLatin1Char(' ') + row.what));
            QTest::keyClick(w.get(), row.keys.at(0).toLatin1());
            QVERIFY2(button->isChecked(), qPrintable(row.keys));
        }
    }

    void onlyTheRightClickButtonsCarryTheMark() {
        auto w = open(true, QSize(1000, 680));
        QVERIFY(QTest::qWaitForWindowExposed(w.get()));
        QStringList marked;
        for (auto *mark : w->findChildren<QWidget *>(QStringLiteral("ContextMark")))
            marked << mark->parentWidget()->objectName();
        marked.sort();
        QCOMPARE(marked, (QStringList{QStringLiteral("Copy"), QStringLiteral("PlaybackMute"), QStringLiteral("Save")}));
        for (auto *mark : w->findChildren<QWidget *>(QStringLiteral("ContextMark"))) {
            const QWidget *button = mark->parentWidget();
            QCOMPARE(mark->geometry().bottomRight(), button->rect().bottomRight());   // in the corner
            QVERIFY(mark->testAttribute(Qt::WA_TransparentForMouseEvents));
        }
    }
};

QTEST_MAIN(TestShortcutSheet)
#include "test_shortcutsheet.moc"

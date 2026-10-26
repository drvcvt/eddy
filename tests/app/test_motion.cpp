#include <QtTest>
#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include "app/fills.h"
#include "app/spring.h"
using namespace eddy;

class TestMotion : public QObject {
    Q_OBJECT
private slots:
    void springLandsExactlyWithoutOvershoot() {
        Spring::setEnabled(true);
        QList<qreal> seen;
        Spring spring(motion::kFast, [&](qreal v) { seen << v; });
        spring.jumpTo(0);
        spring.setTarget(10);
        QTRY_VERIFY_WITH_TIMEOUT(spring.state() == QAbstractAnimation::Stopped, 2000);
        QCOMPARE(seen.last(), 10.0);
        for (qreal v : seen) QVERIFY2(v <= 10.0 + 1e-9, qPrintable(QString::number(v)));
        // Settles within about its duration, not after a long tail.
        QVERIFY(seen.size() > 2);
    }
    void retargetKeepsTheShownValue() {
        Spring::setEnabled(true);
        Spring spring(motion::kBase, {});
        spring.jumpTo(0);
        spring.setTarget(100);
        QTest::qWait(60);
        const qreal before = spring.value();
        spring.setTarget(-100);
        const qreal after = spring.value();
        QVERIFY2(qAbs(after - before) < 2.0, qPrintable(QStringLiteral("%1 -> %2").arg(before).arg(after)));
        QVERIFY(before > 0 && before < 100);
    }
    void reducedMotionJumps() {
        Spring::setEnabled(false);
        qreal shown = -1;
        Spring spring(motion::kSlow, [&](qreal v) { shown = v; });
        spring.setTarget(5);
        QCOMPARE(shown, 5.0);
        QCOMPARE(spring.state(), QAbstractAnimation::Stopped);
        Spring::setEnabled(true);
    }
    void hoverFillEasesInAndOut() {
        Spring::setEnabled(true);
        HoverFills::install();
        QWidget window;
        auto *button = new QToolButton(&window);
        button->setFixedSize(28, 28);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(button, &enter);
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(button), 1.0, 2000);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(button, &leave);
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(button), 0.0, 2000);
    }
    void selectionGlidesToTheNewChoice() {
        Spring::setEnabled(true);
        QWidget rail;
        auto *layout = new QVBoxLayout(&rail);
        QButtonGroup group;
        QList<QToolButton *> buttons;
        for (int i = 0; i < 3; ++i) {
            auto *b = new QToolButton(&rail);
            b->setCheckable(true);
            b->setFixedSize(28, 28);
            group.addButton(b);
            layout->addWidget(b);
            buttons << b;
        }
        buttons[0]->setChecked(true);
        SelectionGlide glide(&group, &rail, theme::Tone::Raise3);
        rail.show();
        QVERIFY(QTest::qWaitForWindowExposed(&rail));
        QTRY_COMPARE(glide.fillRect().topLeft(), QPointF(buttons[0]->pos()));
        buttons[2]->click();
        QTest::qWait(30);
        const qreal midway = glide.fillRect().top();
        QVERIFY2(midway > buttons[0]->y() && midway < buttons[2]->y(), qPrintable(QString::number(midway)));
        QTRY_COMPARE_WITH_TIMEOUT(glide.fillRect().topLeft(), QPointF(buttons[2]->pos()), 2000);
    }
};

QTEST_MAIN(TestMotion)
#include "test_motion.moc"

#include <QtTest>
#include <cmath>
#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
#include "app/fills.h"
#include "app/spring.h"
#include "app/theme.h"
#include <QMenu>
#include <QScopeGuard>
#include <QTimer>
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
    void glideTakesTheWidthAndFadesWhenNothingIsChosen() {
        // Segments differ in width (Auto, 16:9); a mixed selection checks none of them.
        Spring::setEnabled(true);
        QWidget row;
        auto *layout = new QHBoxLayout(&row);
        QButtonGroup group;
        QList<QToolButton *> buttons;
        for (int width : {30, 80}) {
            auto *b = new QToolButton(&row);
            b->setCheckable(true);
            b->setFixedSize(width, 26);
            group.addButton(b);
            layout->addWidget(b);
            buttons << b;
        }
        buttons[0]->setChecked(true);
        SelectionGlide glide(&group, &row, theme::Tone::Raise3, SelectionGlide::Shape::Chip);
        row.show();
        QVERIFY(QTest::qWaitForWindowExposed(&row));
        QTRY_COMPARE(glide.fillRect(), QRectF(buttons[0]->geometry()));
        buttons[1]->click();
        QTest::qWait(30);
        const qreal width = glide.fillRect().width();
        QVERIFY2(width > 30 && width < 80, qPrintable(QString::number(width)));
        QTRY_COMPARE_WITH_TIMEOUT(glide.fillRect(), QRectF(buttons[1]->geometry()), 2000);
        group.setExclusive(false);
        buttons[1]->setChecked(false);
        QTest::qWait(30);
        QVERIFY(glide.opacity() > 0 && glide.opacity() < 1);   // fades, not gone at once
        QTRY_COMPARE_WITH_TIMEOUT(glide.opacity(), 0.0, 2000);
        // Chosen again from nothing, it shows where it is instead of flying in.
        buttons[0]->setChecked(true);
        QCOMPARE(glide.fillRect(), QRectF(buttons[0]->geometry()));
        QTRY_COMPARE_WITH_TIMEOUT(glide.opacity(), 1.0, 2000);
    }
    void aToggleEasesItsFillInAndOut() {
        Spring::setEnabled(true);
        HoverFills::install();
        QWidget window;
        auto *button = new QToolButton(&window);
        button->setCheckable(true);
        button->setFixedSize(26, 26);
        easeToggle(button);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        button->repaint();
        QCOMPARE(HoverFills::checkedLevel(button), 0.0);
        button->setChecked(true);
        button->repaint();
        QTest::qWait(30);
        const qreal midway = HoverFills::checkedLevel(button);
        QVERIFY2(midway > 0 && midway < 1, qPrintable(QString::number(midway)));
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::checkedLevel(button), 1.0, 2000);
        button->setChecked(false);
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::checkedLevel(button), 0.0, 2000);
        // Reduced motion: it lands on the next paint.
        Spring::setEnabled(false);
        button->setChecked(true);
        button->repaint();
        QCOMPARE(HoverFills::checkedLevel(button), 1.0);
        Spring::setEnabled(true);
    }
    void primaryFillsEaseOneStepOnHover() {
        // Save, the toast's action, Resume and Crop's Apply ignored the pointer, or
        // (Apply) jumped to an inverted white fill.
        Spring::setEnabled(true);
        HoverFills::install();
        // As in the app: the stylesheet leaves their fill to HoverFills.
        const bool dark = true;
        qApp->setPalette(theme::palette(dark));
        qApp->setStyleSheet(theme::styleSheet(dark));
        const auto plain = qScopeGuard([] { qApp->setStyleSheet({}); });
        for (const char *name : {"ExportSave", "CropApply"}) {
            QWidget window;
            window.setObjectName(QLatin1String(name) == QLatin1String("CropApply") ? QStringLiteral("CropBar")
                                                                                  : QStringLiteral("ExportPanel"));
            auto *button = new QToolButton(&window);
            button->setObjectName(QString::fromLatin1(name));
            button->setFixedSize(80, 26);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            const auto shown = [button] {
                QImage image(button->size(), QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                button->render(&image);
                return image.pixelColor(40, 13);
            };
            const QColor rest = shown();
            QCOMPARE(rest.alpha(), 255);   // the fill is there at rest
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(button, &enter);
            QTest::qWait(30);
            const qreal midway = HoverFills::level(button);
            QVERIFY2(midway > 0 && midway < 1, qPrintable(QString::number(midway)));
            QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(button), 1.0, 2000);
            const QColor hover = shown();
            const int step = std::abs(hover.lightness() - rest.lightness());
            QVERIFY2(step >= 8 && step <= 60, qPrintable(QStringLiteral("%1: %2 -> %3").arg(name, rest.name(), hover.name())));
            if (QLatin1String(name) == QLatin1String("CropApply"))
                QVERIFY(hover != theme::tone(theme::Tone::Fg, dark) || rest == theme::tone(theme::Tone::Fg, dark));
        }
    }
    // A: a press is its own step, eased on `fast` like the hover (after Boltsnap's Press channel).
    void aPressAddsAStepAndLetsGoOffTheButton() {
        Spring::setEnabled(true);
        HoverFills::install();
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
        const auto plain = qScopeGuard([] { qApp->setStyleSheet({}); });
        QWidget bar;
        bar.setObjectName(QStringLiteral("PlaybackBar"));   // a ghost on the frame's grey
        bar.setAttribute(Qt::WA_StyledBackground, true);
        auto *button = new QToolButton(&bar);
        button->setAutoRaise(true);
        button->setFixedSize(26, 26);
        button->move(10, 10);
        bar.resize(60, 46);
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));
        const auto lightness = [&] { return bar.grab().toImage().pixelColor(23, 23).lightness(); };
        const int rest = lightness();
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(button, &enter);
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(button), 1.0, 2000);
        const int hover = lightness();
        QTest::mousePress(button, Qt::LeftButton, {}, QPoint(13, 13));
        QTest::qWait(30);
        const qreal midway = HoverFills::pressLevel(button);
        QVERIFY2(midway > 0 && midway < 1, qPrintable(QString::number(midway)));
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::pressLevel(button), 1.0, 2000);
        const int pressed = lightness();
        qInfo("ghost in a bar: rest %d, hover %d, pressed %d", rest, hover, pressed);
        QVERIFY(rest < hover && hover < pressed);
        // Off the button with the mouse still down: Qt lets go of it, and so does the step.
        QMouseEvent away(QEvent::MouseMove, QPointF(60, 60), button->mapToGlobal(QPointF(60, 60)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(button, &away);
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::pressLevel(button), 0.0, 2000);
        QTest::mouseRelease(button, Qt::LeftButton, {}, QPoint(60, 60));
        // Disabled, a press shows nothing.
        button->setEnabled(false);
        QTest::mousePress(button, Qt::LeftButton, {}, QPoint(13, 13));
        QTest::qWait(150);
        QCOMPARE(HoverFills::pressLevel(button), 0.0);
        QTest::mouseRelease(button, Qt::LeftButton, {}, QPoint(13, 13));
        // Motion off: the step lands at once.
        button->setEnabled(true);
        Spring::setEnabled(false);
        const auto on = qScopeGuard([] { Spring::setEnabled(true); });
        QTest::mousePress(button, Qt::LeftButton, {}, QPoint(13, 13));
        QApplication::processEvents();
        QCOMPARE(HoverFills::pressLevel(button), 1.0);
        QTest::mouseRelease(button, Qt::LeftButton, {}, QPoint(13, 13));
    }
    void aMenuButtonStaysDownWhileItsMenuIsOpen() {
        Spring::setEnabled(true);
        HoverFills::install();
        QWidget window;
        auto *button = new QToolButton(&window);
        button->setFixedSize(40, 26);
        auto *menu = new QMenu(button);
        menu->addAction(QStringLiteral("2×"));
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        qreal whileOpen = -1;
        QTimer::singleShot(400, menu, [&] { whileOpen = HoverFills::pressLevel(button); menu->close(); });
        button->showMenu();   // blocks until the timer closes it
        QVERIFY2(whileOpen > 0.98, qPrintable(QString::number(whileOpen)));
        QTRY_COMPARE_WITH_TIMEOUT(HoverFills::pressLevel(button), 0.0, 2000);
    }
    // A2 and A0: a raised control has its fill at rest, a step above its surface; its hover
    // eases (it used to switch in the stylesheet), and disabled it keeps the fill.
    void aRaisedControlEasesFromItsRestingFill() {
        Spring::setEnabled(true);
        HoverFills::install();
        for (const bool dark : {true, false}) {
            qApp->setPalette(theme::palette(dark));
            qApp->setStyleSheet(theme::styleSheet(dark));
            const auto plain = qScopeGuard([] { qApp->setStyleSheet({}); });
            for (const char *surface : {"PlaybackBar", "FragmentBar"}) {
                QWidget bar;
                bar.setObjectName(QString::fromLatin1(surface));
                auto *button = new QToolButton(&bar);
                button->setText(QStringLiteral("Join"));
                button->setAutoRaise(true);
                raiseAtRest(button);
                button->setFixedSize(60, 26);
                bar.resize(80, 40);
                bar.show();
                QVERIFY(QTest::qWaitForWindowExposed(&bar));
                const auto fill = [&] { return button->grab().toImage().pixelColor(4, 13).rgb(); };
                const QColor expected = theme::tone(QByteArray(surface) == "PlaybackBar" ? theme::Tone::Raise1
                                                                                       : theme::Tone::Raise2, dark);
                QCOMPARE(fill(), expected.rgb());
                QEvent enter(QEvent::Enter);
                QApplication::sendEvent(button, &enter);
                QTest::qWait(30);
                const QColor midway = QColor(fill());
                QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(button), 1.0, 2000);
                const QColor hovered = QColor(fill());
                const auto between = [](int a, int m, int b) { return (a < m && m < b) || (b < m && m < a); };
                QVERIFY2(between(expected.lightness(), midway.lightness(), hovered.lightness()),
                         qPrintable(QStringLiteral("%1 %2 %3").arg(expected.name(), midway.name(), hovered.name())));
                QEvent leave(QEvent::Leave);
                QApplication::sendEvent(button, &leave);
                QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(button), 0.0, 2000);
                button->setEnabled(false);
                QCOMPARE(fill(), expected.rgb());   // the fill stays; the label goes faint
                QCOMPARE(button->palette().color(QPalette::Disabled, QPalette::ButtonText).rgb(),
                         theme::tone(theme::Tone::Faint, dark).rgb());
            }
        }
    }
    void aPressedPrimaryKeepsItsTextReadable() {
        Spring::setEnabled(false);
        const auto on = qScopeGuard([] { Spring::setEnabled(true); });
        HoverFills::install();
        const auto luminance = [](const QColor &c) {
            auto channel = [](qreal v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
        };
        for (const bool dark : {true, false}) {
            qApp->setPalette(theme::palette(dark));
            qApp->setStyleSheet(theme::styleSheet(dark));   // as in the app: the fill is HoverFills' alone
            const auto plain = qScopeGuard([] { qApp->setStyleSheet({}); });
            QWidget window;
            auto *save = new QToolButton(&window);
            save->setObjectName(QStringLiteral("ExportSave"));
            save->setFixedSize(60, 26);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTest::mousePress(save, Qt::LeftButton, {}, QPoint(30, 13));
            QApplication::processEvents();
            const QColor held = save->grab().toImage().pixelColor(4, 13);
            QTest::mouseRelease(save, Qt::LeftButton, {}, QPoint(70, 13));
            const QColor text = theme::tone(theme::Tone::Bg, dark);
            const qreal a = luminance(held), b = luminance(text);
            const qreal ratio = (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
            qInfo("%s: pressed primary %s, text %s: %.2f:1", dark ? "dark" : "light",
                  qPrintable(held.name()), qPrintable(text.name()), ratio);
            QVERIFY(ratio >= 4.5);
        }
    }
    void fadeVisibleTurnsBackAndLetsTheMouseThrough() {
        Spring::setEnabled(true);
        QWidget window;
        window.resize(200, 100);
        auto *bar = new QWidget(&window);
        bar->setGeometry(10, 10, 80, 30);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto opacity = [bar] {
            auto *effect = qobject_cast<QGraphicsOpacityEffect *>(bar->graphicsEffect());
            return effect && effect->isEnabled() ? effect->opacity() : 1.0;
        };
        fadeVisible(bar, false);
        QVERIFY(bar->testAttribute(Qt::WA_TransparentForMouseEvents));   // gone for the pointer at once
        QTest::qWait(40);
        QVERIFY(bar->isVisible());
        const qreal midway = opacity();
        QVERIFY2(midway > 0 && midway < 1, qPrintable(QString::number(midway)));
        fadeVisible(bar, true);   // back before it is gone: it turns, never hides
        QVERIFY(!bar->testAttribute(Qt::WA_TransparentForMouseEvents));
        QTRY_COMPARE_WITH_TIMEOUT(opacity(), 1.0, 2000);
        QVERIFY(bar->isVisible());
        fadeVisible(bar, false);
        QTRY_VERIFY_WITH_TIMEOUT(bar->isHidden(), 2000);
        // From hidden it starts transparent and comes in.
        fadeVisible(bar, true);
        QVERIFY(bar->isVisible() && opacity() < 0.5);
        QTRY_COMPARE_WITH_TIMEOUT(opacity(), 1.0, 2000);
        // Motion off: at once.
        Spring::setEnabled(false);
        fadeVisible(bar, false);
        QVERIFY(bar->isHidden());
        Spring::setEnabled(true);
    }
    void labelsBlendWithTheirHover() {
        // The label used to jump to @fg the moment the pointer arrived, under a fill that eased in.
        Spring::setEnabled(true);
        HoverFills::install();
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
        const auto plain = qScopeGuard([] { qApp->setStyleSheet({}); });
        QWidget window;
        QToolButton *plainButton = new QToolButton(&window);
        QToolButton *capButton = new theme::CapButton(&window);
        for (QToolButton *b : {plainButton, capButton}) {
            b->setText(QStringLiteral("Hover me"));
            b->setFixedSize(120, 26);
            b->setFocusPolicy(Qt::NoFocus);   // focus brightens the label on its own
        }
        capButton->move(0, 40);
        window.resize(140, 80);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto brightest = [](QToolButton *b) {
            QImage image(b->size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
            b->render(&image);
            int most = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) most = std::max(most, qGray(image.pixel(x, y)));
            return most;
        };
        for (QToolButton *b : {plainButton, capButton}) {
            const int rest = brightest(b);
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(b, &enter);
            QTest::qWait(40);
            const qreal level = HoverFills::level(b);
            const int midway = brightest(b);
            QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(b), 1.0, 2000);
            const int hovered = brightest(b);
            qInfo("%s: rest %d, at %.2f %d, hovered %d", b->metaObject()->className(), rest, level, midway, hovered);
            QVERIFY(hovered > rest + 20);
            QVERIFY2(midway > rest && midway < hovered, qPrintable(QString::number(midway)));
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(b, &leave);
            QTRY_COMPARE_WITH_TIMEOUT(HoverFills::level(b), 0.0, 2000);
        }
    }
};

QTEST_MAIN(TestMotion)
#include "test_motion.moc"

#include <QtTest>
#include <QButtonGroup>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsOpacityEffect>
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

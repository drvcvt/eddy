// The ui skill's gates, measured on the real render in the dark theme: capitals
// centred in their control, icons centred on the capitals beside them, one icon
// axis down the tool rail, and nothing past the window at its narrowest.
#include "editortest.h"
#include <QPainter>
#include <QListWidget>
#include <QMenu>
#include <QTimer>
#include <QScrollBar>
#include <QStyleOptionSlider>

namespace {

// Ink edges of `region` against `background`, each edge interpolated by the
// coverage of its outermost row or column (layout.md, optical geometry).
struct Ink { qreal top = 0, bottom = 0, left = 0, right = 0; bool any = false; };

qreal coverage(QColor pixel, QColor background, QColor ink) {
    const auto d = [](QColor a, QColor b) {
        return std::abs(a.redF() - b.redF()) + std::abs(a.greenF() - b.greenF()) + std::abs(a.blueF() - b.blueF());
    };
    const qreal full = d(ink, background);
    return full <= 0 ? 0 : std::clamp(d(pixel, background) / full, 0.0, 1.0);
}

Ink inkOf(const QImage &image, QRect region, QColor background, QColor ink) {
    region &= image.rect();
    QVector<qreal> rows(region.height()), cols(region.width());
    for (int y = 0; y < region.height(); ++y)
        for (int x = 0; x < region.width(); ++x) {
            const qreal c = coverage(image.pixelColor(region.x() + x, region.y() + y), background, ink);
            rows[y] = std::max(rows[y], c);
            cols[x] = std::max(cols[x], c);
        }
    Ink out;
    const auto first = [](const QVector<qreal> &v) { for (int i = 0; i < v.size(); ++i) if (v[i] > 0.05) return i; return -1; };
    const auto last = [](const QVector<qreal> &v) { for (int i = v.size() - 1; i >= 0; --i) if (v[i] > 0.05) return i; return -1; };
    const int t = first(rows), b = last(rows), l = first(cols), r = last(cols);
    if (t < 0 || l < 0) return out;
    out.any = true;
    out.top = region.y() + t + (1 - rows[t]);
    out.bottom = region.y() + b + rows[b];
    out.left = region.x() + l + (1 - cols[l]);
    out.right = region.x() + r + cols[r];
    return out;
}

QString path(const QObject *o) {
    QStringList names;
    for (; o; o = o->parent()) if (!o->objectName().isEmpty()) names.prepend(o->objectName());
    return names.join(QLatin1Char('/'));
}

std::unique_ptr<EditorWindow> openVideo(const QString &file, QSize size, bool audio = false) {
    MediaDocument doc;
    doc.kind = MediaKind::Video;
    doc.path = file;
    doc.video = {QSize(64, 48), 4000, 25.0};
    doc.video.hasAudio = audio;   // the file is missing: the waveform lane says so, at full height
    Config cfg; cfg.animations = false;
    auto window = std::make_unique<EditorWindow>(doc, cfg, CliOptions());
    window->resize(size);
    window->show();
    return window;
}

}

class TestUiGates : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qApp->setPalette(theme::palette(true));
        qApp->setStyleSheet(theme::styleSheet(true));
    }

    void capitalsSitInTheMiddleOfTheirControl() {
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("gates.mp4")), QSize(1000, 680));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QStringList off;
        int measured = 0;
        for (QToolButton *button : window->findChildren<QToolButton *>()) {
            if (!button->isVisible() || button->text().isEmpty()) continue;
            if (!button->icon().isNull() && button->toolButtonStyle() == Qt::ToolButtonIconOnly) continue;
            // Capitals only, so no ascender or descender takes part (layout.md 6).
            const QString text = button->text();
            button->setText(text.toUpper());
            const QImage shot = button->grab().toImage();
            button->setText(text);
            const QColor background = shot.pixelColor(1, 1);
            const QColor ink = button->palette().color(QPalette::ButtonText);
            // Text only: right of any icon, which LabelButton draws at the left.
            const int textLeft = button->icon().isNull() ? 0 : button->iconSize().width() + theme::kS2;
            const Ink caps = inkOf(shot, QRect(textLeft, 0, shot.width() - textLeft, shot.height()), background, ink);
            if (!caps.any) continue;
            ++measured;
            const qreal delta = (caps.top + caps.bottom) / 2 - shot.height() / 2.0;
            qInfo("centre %s \"%s\": %+.2f px", qPrintable(path(button)), qPrintable(text), delta);
            if (std::abs(delta) > 0.5)
                off << QStringLiteral("%1 \"%2\": %3 px").arg(path(button), text).arg(delta, 0, 'f', 2);
        }
        QVERIFY2(measured >= 4, qPrintable(QString::number(measured)));
        QVERIFY2(off.isEmpty(), qPrintable(off.join(QStringLiteral("\n   "))));
    }

    void dragPillWordSitsOnItsMiddle() {
        // A QLabel centres its line box; the capitals sat a pixel low beside the grip.
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("pill.mp4")), QSize(1000, 680));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        auto *pill = window->findChild<DragPill *>();
        auto *label = pill->findChild<QLabel *>(QStringLiteral("DragPillText"));
        auto *icon = pill->findChild<QLabel *>(QStringLiteral("DragPillIcon"));
        QVERIFY(pill && label && icon);
        const QImage shot = pill->grab().toImage();
        const QColor background = shot.pixelColor(1, 1);
        const QColor ink = label->palette().color(QPalette::WindowText);
        // The first capital only: "D" has no descender and no ascender above the caps.
        const QRect text(label->mapTo(pill, QPoint()), label->size());
        const int startX = text.x() + int((label->width() - label->fontMetrics().horizontalAdvance(label->text())) / 2);
        const Ink caps = inkOf(shot, QRect(startX, 0, label->fontMetrics().horizontalAdvance(QStringLiteral("D")), shot.height()),
                               background, ink);
        const Ink grip = inkOf(shot, QRect(icon->mapTo(pill, QPoint()), icon->size()), background, ink);
        QVERIFY(caps.any && grip.any);
        const qreal capsMiddle = (caps.top + caps.bottom) / 2, gripMiddle = (grip.top + grip.bottom) / 2;
        qInfo("pill: capitals %+.2f px off the middle, grip %+.2f px off the capitals",
              capsMiddle - pill->height() / 2.0, gripMiddle - capsMiddle);
        QVERIFY(std::abs(capsMiddle - pill->height() / 2.0) <= 0.5);
        QVERIFY(std::abs(gripMiddle - capsMiddle) <= 0.5);
    }

    void contextBarsSeparateTheirGroups() {
        // Buttons kS1 apart, groups kS2: sizes | bold | alignment | fill.
        TextBar text;
        text.show();
        QVERIFY(QTest::qWaitForWindowExposed(&text));
        const auto gap = [](QWidget *bar, const char *a, const char *b) {
            const QWidget *left = bar->findChild<QWidget *>(QString::fromLatin1(a));
            const QWidget *right = bar->findChild<QWidget *>(QString::fromLatin1(b));
            return right->geometry().left() - left->geometry().right() - 1;
        };
        QCOMPARE(gap(&text, "TextSize14", "TextSize20"), theme::kS1);
        QCOMPARE(gap(&text, "TextSize28", "TextBold"), theme::kS2);
        QCOMPARE(gap(&text, "TextBold", "TextAlignLeft"), theme::kS2);
        QCOMPARE(gap(&text, "TextAlignRight", "TextFill"), theme::kS2);
        // Without the video's time scopes the bar keeps even padding: no gap left over.
        RedactBar redact;
        redact.setTimeScope(false, false);
        redact.show();
        QVERIFY(QTest::qWaitForWindowExposed(&redact));
        int right = 0;
        for (auto *b : redact.findChildren<QToolButton *>())
            if (b->isVisible()) right = std::max(right, b->geometry().right());
        QCOMPARE(redact.width() - 1 - right, theme::kS1);
    }

    void listsScrollOnASlimGrip() {
        // The Resume list showed the platform's bar with arrows and a trough.
        QListWidget list;
        for (int i = 0; i < 60; ++i) list.addItem(QStringLiteral("entry %1").arg(i));
        list.resize(300, 200);
        list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));
        QScrollBar *bar = list.verticalScrollBar();
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->width(), theme::kS3);   // an 8 px grip kS1 off the rows
        QStyleOptionSlider option;
        option.initFrom(bar);
        option.orientation = Qt::Vertical;
        option.minimum = bar->minimum(); option.maximum = bar->maximum();
        option.sliderPosition = bar->sliderPosition(); option.pageStep = bar->pageStep();
        const QRect arrow = bar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarAddLine, bar);
        QVERIFY2(arrow.height() <= 0, qPrintable(QString::number(arrow.height())));
    }

    void popoversOpenUnderTheBarInsideTheWindow() {
        // Colour, Studio and Export opened at three different places; Export overlapped
        // the bar by 5 px and could run past the window's right edge.
        QImage image(800, 500, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        for (const int width : {1000, 760}) {
            EditorWindow window(image, cfg, CliOptions());
            window.resize(width, 600);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            auto *bar = window.findChild<QWidget *>(QStringLiteral("Toolbar"));
            const int top = bar->mapToGlobal(QPoint(0, bar->height())).y() + theme::kS1;
            const int right = window.mapToGlobal(QPoint(window.width(), 0)).x() - theme::kBarPad;
            const auto check = [&](QWidget *popup, const char *name) {
                QVERIFY2(popup, name);
                const QRect at(popup->mapToGlobal(QPoint()), popup->size());
                QVERIFY2(at.top() == top, qPrintable(QStringLiteral("%1 top %2, want %3").arg(name).arg(at.top()).arg(top)));
                QVERIFY2(at.right() < right + 1, qPrintable(QStringLiteral("%1 right %2 past %3").arg(name).arg(at.right()).arg(right)));
            };
            window.findChild<QToolButton *>(QStringLiteral("Swatch"))->click();
            QApplication::processEvents();
            QWidget *colour = nullptr;
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->objectName() == QStringLiteral("ColorPopover") && w->isVisible()) colour = w;
            check(colour, "colour");
            colour->close();
            window.findChild<QToolButton *>(QStringLiteral("Studio"))->click();
            QApplication::processEvents();
            QWidget *studio = nullptr;
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->objectName() == QStringLiteral("StudioPopover") && w->isVisible()) studio = w;
            check(studio, "studio");
            studio->close();
            auto *save = window.findChild<QToolButton *>(QStringLiteral("Save"));
            QMenu *menu = save->menu();
            QTimer::singleShot(50, menu, [&] { check(menu, "export"); menu->close(); });
            save->showMenu();
        }
    }

    void footerStatusNeverCoversThePill() {
        // At the narrowest video window, with every status shown at once (an export running,
        // Cancel, and the output without sound), the right group stays clear of the pill.
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("footer.mp4")), QSize(520, 620), true);
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        window->findChild<QLabel *>(QStringLiteral("VideoExportStatus"))->setText(QStringLiteral("Cancelling…"));
        window->findChild<QWidget *>(QStringLiteral("VideoExportCancel"))->show();
        window->findChild<QWidget *>(QStringLiteral("NoAudio"))->show();
        QApplication::processEvents();
        const auto rect = [&](const char *name) {
            auto *w = window->findChild<QWidget *>(QString::fromLatin1(name));
            return QRect(w->mapTo(window.get(), QPoint()), w->size());
        };
        const QRect pill = rect("DragPill");
        for (const char *name : {"VideoExportStatus", "VideoExportCancel", "NoAudio"}) {
            const QRect r = rect(name);
            qInfo("%s %d..%d, pill %d..%d", name, r.left(), r.right(), pill.left(), pill.right());
            QVERIFY2(!r.intersects(pill), name);
        }
    }

    void toolRailIconsShareOneAxis() {
        QImage image(800, 500, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, CliOptions());
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QWidget *rail = window.findChild<QWidget *>(QStringLiteral("ToolRail"));
        QVERIFY(rail);
        const QImage shot = rail->grab().toImage();
        const QColor background = shot.pixelColor(1, 1);
        QVector<qreal> centres;
        for (QToolButton *button : rail->findChildren<QToolButton *>()) {
            if (button->isChecked()) continue;   // the glide fill shifts its background
            const QRect box(button->geometry().adjusted(2, 2, -2, -2));
            const Ink ink = inkOf(shot, box, background, theme::tone(theme::Tone::Sub, true));
            QVERIFY2(ink.any, qPrintable(button->objectName()));
            centres << (ink.left + ink.right) / 2;
            // Every rail button: the same distance to the rail's edge on all sides.
            QCOMPARE(button->x(), theme::kBarPad);
        }
        QVERIFY(centres.size() >= 8);
        const auto [lo, hi] = std::minmax_element(centres.begin(), centres.end());
        // Drawn on one keyline grid, the glyphs' ink centres differ by their shape only.
        QVERIFY2(*hi - *lo <= 1.0, qPrintable(QStringLiteral("icon centres spread %1 px").arg(*hi - *lo)));
    }

    void labelledIconSitsOnTheCapitals() {
        QImage image(800, 500, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, CliOptions());
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QStringList off;
        for (const char *name : {"Studio", "SendToShelf"}) {
            auto *button = window.findChild<QToolButton *>(QString::fromLatin1(name));
            QVERIFY2(button, name);
            const QString text = button->text();
            button->setText(text.toUpper());
            const QSize laidOut = button->size();
            button->resize(button->sizeHint());   // capitals run wider; keep the content at its padding
            const QImage shot = button->grab().toImage();
            button->setText(text);
            button->resize(laidOut);
            const QColor background = shot.pixelColor(1, 1);
            const QColor ink = button->palette().color(QPalette::ButtonText);
            const int split = theme::kS2 + theme::iconInk(button->iconSize().width()) + theme::kS2 / 2;
            const Ink icon = inkOf(shot, QRect(0, 0, split, shot.height()), background, ink);
            const Ink caps = inkOf(shot, QRect(split, 0, shot.width() - split, shot.height()), background, ink);
            QVERIFY2(icon.any && caps.any, name);
            const qreal delta = (icon.top + icon.bottom) / 2 - (caps.top + caps.bottom) / 2;
            if (std::abs(delta) > 0.5) off << QStringLiteral("%1: icon %2 px off the capitals").arg(name).arg(delta, 0, 'f', 2);
            const qreal gap = caps.left - icon.right;
            qInfo("%s: icon %+.2f px off the capitals, %.2f px to the text", name, delta, gap);
            // The text origin is kS2 past the ink; a letter's own side bearing adds up to a pixel.
            if (gap < theme::kS2 - 0.5 || gap > theme::kS2 + 1.5)
                off << QStringLiteral("%1: icon to text %2 px").arg(name).arg(gap, 0, 'f', 2);
        }
        QVERIFY2(off.isEmpty(), qPrintable(off.join(QStringLiteral("\n   "))));
    }

    void studioOnWearsTheWidthMark() {
        // Two "on" marks in one bar: the same inset squircle rows and the same tone.
        QImage image(800, 500, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, CliOptions());
        window.resize(1000, 680);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *studio = window.findChild<QToolButton *>(QStringLiteral("Studio"));
        auto *width = window.findChild<QToolButton *>(QStringLiteral("WidthM"));
        QVERIFY(studio && width && width->isChecked());
        studio->setChecked(true);
        QApplication::processEvents();
        const QImage bar = window.findChild<QWidget *>(QStringLiteral("Toolbar"))->grab().toImage();
        const QColor background = bar.pixelColor(1, 1);
        auto mark = [&](QToolButton *b) {
            const QPoint at = b->mapTo(window.findChild<QWidget *>(QStringLiteral("Toolbar")), QPoint());
            const int x = at.x() + b->width() / 2;
            int top = -1, bottom = -1;
            for (int y = at.y(); y < at.y() + b->height(); ++y)
                if (bar.pixelColor(x, y) != background) { if (top < 0) top = y - at.y(); bottom = y - at.y(); }
            return std::tuple{top, bottom, bar.pixelColor(x, at.y() + theme::kMarkInset + 1)};
        };
        const auto [studioTop, studioBottom, studioTone] = mark(studio);
        const auto [widthTop, widthBottom, widthTone] = mark(width);
        QCOMPARE(studioTop, widthTop);
        QCOMPARE(studioBottom, widthBottom);
        QCOMPARE(studioTone, widthTone);
    }

    void videoControlsShareTheirAxes() {
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("axes.mp4")), QSize(1080, 700));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        const auto rect = [&](const char *name) {
            auto *w = window->findChild<QWidget *>(QString::fromLatin1(name));
            return w ? QRect(w->mapTo(window.get(), QPoint()), w->size()) : QRect();
        };
        const QRect timeline = rect("VideoTimeline"), play = rect("PlaybackPlay"), fit = rect("ZoomFit");
        const QRect mute = rect("PlaybackMute"), status = rect("VideoExportStatus");
        const QRect trim = rect("ClipControls"), pill = rect("DragPill");
        // One left and one right edge for the whole bottom, the tool rail's.
        for (const QRect &r : {timeline, play, fit}) QCOMPARE(r.left(), theme::kBarPad);
        for (const QRect &r : {timeline, mute, status}) QCOMPARE(window->width() - 1 - r.right(), theme::kBarPad);
        // The clip's controls and the drag pill on the window's middle.
        QVERIFY2(qAbs(trim.center().x() - window->width() / 2) <= 1, qPrintable(QString::number(trim.center().x())));
        QVERIFY2(qAbs(pill.center().x() - window->width() / 2) <= 1, qPrintable(QString::number(pill.center().x())));
        // The transport row kS2 under the film strip it drives,
        // the footer kS3 under it: the row belongs to the timeline, not to the footer.
        const int strip = timeline.top() + int(VideoTimeline::kTrackTop + VideoTimeline::kTrackH);
        QCOMPARE(timeline.bottom() + 1, strip);   // nothing empty under the last lane
        QCOMPARE(trim.top() - strip, theme::kS2);
        QCOMPARE(fit.top() - (trim.bottom() + 1), theme::kS3);
        // The row's controls share its middle.
        for (const QRect &r : {play, mute}) QCOMPARE(r.center().y(), trim.center().y());
    }

    void clipButtonsReadAsControls() {
        // K and B: Split and Delete stand on a raised fill at rest (Boltsnap's lesson), one
        // height, kS2 apart; capitals and icon ink on the fill's middle, measured on the ink.
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("clip.mp4")), QSize(1080, 700));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        auto *split = window->findChild<QToolButton *>(QStringLiteral("ClipSplit"));
        auto *remove = window->findChild<QToolButton *>(QStringLiteral("ClipDelete"));
        QVERIFY(split && remove);
        QCOMPARE(split->height(), theme::kCtl);
        QCOMPARE(remove->height(), theme::kCtl);
        QCOMPARE(remove->geometry().left() - split->geometry().right() - 1, theme::kS2);
        QCOMPARE(split->geometry().center().y(), remove->geometry().center().y());
        auto *bar = window->findChild<QWidget *>(QStringLiteral("PlaybackBar"));
        const QImage row = bar->grab().toImage();
        const QPoint inside = split->mapTo(bar, QPoint(4, split->height() / 2));
        const QPoint between = split->mapTo(bar, QPoint(split->width() + theme::kS2 / 2, split->height() / 2));
        QVERIFY2(row.pixelColor(inside) != row.pixelColor(between),
                 qPrintable(row.pixelColor(inside).name() + " on " + row.pixelColor(between).name()));
        emit window->findChild<VideoTimeline *>()->stripClicked(1000);   // a chosen piece: Delete at rest, not disabled
        QVERIFY(remove->isEnabled());
        for (QToolButton *button : {split, remove}) {
            // Side room on the label as it is: icon ink to the left edge, text ink to the right.
            const QImage plain = button->grab().toImage();
            const QColor plainFill = plain.pixelColor(4, plain.height() / 2);
            const QColor plainInk = button->palette().color(QPalette::ButtonText);
            const Ink all = inkOf(plain, plain.rect().adjusted(2, 0, -2, 0), plainFill, plainInk);
            const qreal left = all.left, right = plain.width() - all.right;
            qInfo("%s: side room %.1f / %.1f px", qPrintable(button->text()), left, right);
            QVERIFY2(std::abs(left - right) <= 1 && std::abs(left - theme::kS3) <= 1.5,
                     qPrintable(QStringLiteral("%1 / %2").arg(left).arg(right)));
            const QString text = button->text();
            button->setText(text.toUpper());
            const QImage shot = button->grab().toImage();
            button->setText(text);
            const QColor fill = shot.pixelColor(4, shot.height() / 2);
            const QColor ink = button->palette().color(QPalette::ButtonText);
            const int textLeft = theme::kS3 + button->iconSize().width() + theme::kS2 - 2;
            const Ink caps = inkOf(shot, QRect(textLeft, 0, shot.width() - textLeft, shot.height()), fill, ink);
            const Ink icon = inkOf(shot, QRect(theme::kS3 - 2, 0, button->iconSize().width() + 4, shot.height()), fill, ink);
            QVERIFY2(caps.any && icon.any, qPrintable(text));
            const qreal capsMiddle = (caps.top + caps.bottom) / 2, iconMiddle = (icon.top + icon.bottom) / 2;
            qInfo("%s: capitals %+.2f px off the middle, icon %+.2f px off the capitals",
                  qPrintable(text), capsMiddle - shot.height() / 2.0, iconMiddle - capsMiddle);
            QVERIFY(std::abs(capsMiddle - shot.height() / 2.0) <= 0.5);
            QVERIFY(std::abs(iconMiddle - capsMiddle) <= 0.5);
            QVERIFY(icon.bottom - icon.top <= theme::kFsUi);   // no taller than the text beside it
        }
    }
    void choosingARateMovesNothing() {
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("rates.mp4")), QSize(1080, 700));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        auto *speed = window->findChild<QToolButton *>(QStringLiteral("PlaybackSpeed"));
        auto *loop = window->findChild<QWidget *>(QStringLiteral("PlaybackLoop"));
        // The label grows to the left; the arrow at its right edge and loop never move.
        const auto right = [&] { return speed->mapTo(window.get(), QPoint(speed->width(), 0)).x(); };
        const int arrow = right(), x = loop->mapTo(window.get(), QPoint()).x();
        for (QAction *rate : speed->menu()->actions()) {
            rate->trigger();
            QApplication::processEvents();
            QCOMPARE(right(), arrow);
            QCOMPARE(loop->mapTo(window.get(), QPoint()).x(), x);
            QVERIFY(speed->width() <= speed->sizeHint().width());   // fits its label, no gap inside
        }
    }

    void readoutShowsThePositionOnly() {
        // The length is End's and the ruler's; the readout keeps one width for every position.
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("readout.mp4")), QSize(1080, 700));
        auto *time = window->findChild<QLabel *>(QStringLiteral("PlaybackTime"));
        QVERIFY(time);
        QVERIFY2(!time->text().contains(QLatin1Char('/')), qPrintable(time->text()));
        QCOMPARE(time->minimumWidth(), time->maximumWidth());
        QCOMPARE(time->width(), time->fontMetrics().horizontalAdvance(formatPreciseTime(4000)));
    }

    void narrowWindowKeepsEveryControlInside() {
        QTemporaryDir dir;
        // Stress: a long file name in the title and a long message in the toast.
        const QString file = dir.filePath(QStringLiteral("a-very-long-screen-recording-name-from-a-long-afternoon-2026-10-26_23-59-59.mp4"));
        auto window = openVideo(file, QSize(520, 620), true);
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QVERIFY(window->findChild<VideoTimeline *>()->waveformShown());
        QCOMPARE(window->size(), QSize(520, 620));   // the bottom never pushes the window taller
        // Folded onto its own line, the clip's controls keep their size and the window's middle,
        // so its gaps match the wide row.
        auto *trim = window->findChild<QWidget *>(QStringLiteral("ClipControls"));
        QCOMPARE(trim->width(), trim->sizeHint().width());
        const int middle = trim->mapTo(window.get(), trim->rect().center()).x();
        QVERIFY2(qAbs(middle - window->width() / 2) <= 1, qPrintable(QString::number(middle)));
        auto *toast = window->findChild<Toast *>();
        QVERIFY(toast);
        toast->showMessage(QStringLiteral("OCR failed: tesseract exited with code 1 while reading "
                                          "/home/someone/Pictures/an/unusually/deep/folder/screenshot.png"));
        QApplication::processEvents();
        const QRect inside = window->rect();
        QStringList outside;
        for (QWidget *w : window->findChildren<QWidget *>()) {
            if (!w->isVisible() || w->isWindow()) continue;
            if (!qobject_cast<QAbstractButton *>(w) && !qobject_cast<QLabel *>(w) && w != toast) continue;
            const QRect r(w->mapTo(window.get(), QPoint()), w->size());
            if (!inside.contains(r))
                outside << QStringLiteral("%1 at %2,%3 %4x%5").arg(path(w)).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        }
        QVERIFY2(outside.isEmpty(), qPrintable(outside.join(QStringLiteral("\n   "))));
    }
};

QTEST_MAIN(TestUiGates)
#include "test_uigates.moc"

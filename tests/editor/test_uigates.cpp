// The ui skill's gates, measured on the real render in the dark theme: capitals
// centred in their control, icons centred on the capitals beside them, one icon
// axis down the tool rail, and nothing past the window at its narrowest.
#include "editortest.h"
#include <QPainter>

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
        const QRect trim = rect("TrimControls"), pill = rect("DragPill");
        // One left and one right edge for the whole bottom, the tool rail's.
        for (const QRect &r : {timeline, play, fit}) QCOMPARE(r.left(), theme::kBarPad);
        for (const QRect &r : {timeline, mute, status}) QCOMPARE(window->width() - 1 - r.right(), theme::kBarPad);
        // The trim group and the drag pill on the window's middle.
        QVERIFY2(qAbs(trim.center().x() - window->width() / 2) <= 1, qPrintable(QString::number(trim.center().x())));
        QVERIFY2(qAbs(pill.center().x() - window->width() / 2) <= 1, qPrintable(QString::number(pill.center().x())));
        // The transport row (its tallest part, the trim field) kS2 under the film strip it drives,
        // the footer kS3 under it: the row belongs to the timeline, not to the footer.
        const int strip = timeline.top() + int(VideoTimeline::kTrackTop + VideoTimeline::kTrackH);
        QCOMPARE(timeline.bottom() + 1, strip);   // nothing empty under the last lane
        QCOMPARE(trim.top() - strip, theme::kS2);
        QCOMPARE(fit.top() - (trim.bottom() + 1), theme::kS3);
        // The row's controls share its middle.
        for (const QRect &r : {play, mute}) QCOMPARE(r.center().y(), trim.center().y());
    }

    void trimFieldKeepsOneRhythm() {
        // Caption to value the same for Start, End and Duration; pair to pair the same,
        // reset included. Measured on the ink, so padding and side bearings count.
        QTemporaryDir dir;
        auto window = openVideo(dir.filePath(QStringLiteral("rhythm.mp4")), QSize(1080, 700));
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        auto *field = window->findChild<QWidget *>(QStringLiteral("TrimControls"));
        const QImage shot = field->grab().toImage();
        const QColor well = shot.pixelColor(shot.width() / 2, 1);
        const QColor ink = field->palette().color(QPalette::WindowText);
        QVector<Ink> parts;
        for (const char *name : {"TrimSetIn", "TrimInTime", "TrimSetOut", "TrimOutTime",
                                 "TrimDurationCaption", "TrimDuration", "TrimReset"}) {
            auto *w = field->findChild<QWidget *>(QString::fromLatin1(name));
            QVERIFY2(w, name);
            parts << inkOf(shot, QRect(w->mapTo(field, QPoint()), w->size()), well, ink);
            QVERIFY2(parts.last().any, name);
        }
        const auto gap = [&](int a, int b) { return parts[b].left - parts[a].right; };
        const qreal captions[] = {gap(0, 1), gap(2, 3), gap(4, 5)};
        const qreal pairs[] = {gap(1, 2), gap(3, 4), gap(5, 6)};
        qInfo("caption to value %.1f %.1f %.1f, pair to pair %.1f %.1f %.1f",
              captions[0], captions[1], captions[2], pairs[0], pairs[1], pairs[2]);
        for (const qreal g : captions) QVERIFY2(qAbs(g - captions[0]) <= 1, "caption to value differs");
        for (const qreal g : pairs) QVERIFY2(qAbs(g - pairs[0]) <= 1, "pair to pair differs");
        QVERIFY(pairs[0] > captions[0] + theme::kS1);   // pairs read as pairs
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
        // Folded onto its own line, the trim field keeps its size and the window's middle,
        // so its gaps match the wide row.
        auto *trim = window->findChild<QWidget *>(QStringLiteral("TrimControls"));
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

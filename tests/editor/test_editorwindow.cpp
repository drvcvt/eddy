#include "editortest.h"

class TestEditorWindow : public QObject {
    Q_OBJECT
private slots:
    void compactTooltipStaysInsideEditorAndDismisses() {
        QImage image(100, 80, QImage::Format_RGB32); image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, {});
        window.show();
        auto *fit = window.findChild<QToolButton *>("ZoomFit");
        QVERIFY(fit);
        QHelpEvent help(QEvent::ToolTip, fit->rect().center(),
                        fit->mapToGlobal(fit->rect().center()));
        QApplication::sendEvent(fit, &help);
        auto *hint = window.findChild<QLabel *>("CompactTooltip");
        QVERIFY(hint && hint->isVisible());
        // The shortcut is a quieter column, not glued on with a separator glyph.
        QCOMPARE(fit->toolTip(), QStringLiteral("Fit to window\t0"));
        QCOMPARE(hint->text(), theme::tooltipHtml(fit->toolTip()));
        QVERIFY(hint->text().contains(QStringLiteral(">Fit to window<")));
        QVERIFY(hint->text().contains(QStringLiteral(">0<")));
        QVERIFY(window.rect().contains(hint->geometry()));
        QVERIFY(hint->testAttribute(Qt::WA_TransparentForMouseEvents));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(fit, &leave);
        QVERIFY(!hint->isVisible());
    }
    void noTextUsesADotSeparator() {
        // Separators are layout (columns, lines, gaps), never a middle dot.
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("dots.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=2:r=25", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(64, 48), 2000, 25.0};
        Config cfg; cfg.animations = false;
        CliOptions cli; cli.configPath = dir.filePath("config");
        EditorWindow window(doc, cfg, cli);
        window.show();
        QTest::keyClick(&window, Qt::Key_Z);   // zoom bar and mini map exist and show
        window.openStudio();
        const QChar dot(0x00B7);
        QStringList found;
        if (window.windowTitle().contains(dot)) found << window.windowTitle();
        for (QWidget *w : window.findChildren<QWidget *>()) {
            for (const QString &text : {w->toolTip(), w->accessibleName(), w->statusTip()})
                if (text.contains(dot)) found << w->objectName() + ": " + text;
            if (auto *b = qobject_cast<QAbstractButton *>(w); b && b->text().contains(dot))
                found << w->objectName() + ": " + b->text();
            if (auto *l = qobject_cast<QLabel *>(w); l && l->text().contains(dot))
                found << w->objectName() + ": " + l->text();
            for (QAction *a : w->actions())
                if (a->text().contains(dot) || a->toolTip().contains(dot)) found << a->text();
        }
        QVERIFY2(found.isEmpty(), qPrintable(found.join('\n')));
    }
    void chromeFollowsTheDensityRules() {
        // mt-ui-style, measured on the real widgets rather than eyeballed: spacing
        // from one scale, equal surface padding, one height per row, and no icon
        // larger than the label beside it.
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("density.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=4:r=25", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(64, 48), 4000, 25.0};
        Config cfg; cfg.animations = false;
        qApp->setStyleSheet(theme::styleSheet(true));
        const auto restoreSheet = qScopeGuard([] { qApp->setStyleSheet({}); });
        EditorWindow window(doc, cfg, {});
        window.resize(1000, 640);
        window.show();
        QTest::qWait(50);
        static const QSet<int> scale{0, 1, 2, 4, 6, 8, 12, 16, 24, 32, 48, 72};
        QStringList bad;
        auto name = [](const QObject *o) {
            for (; o; o = o->parent()) if (!o->objectName().isEmpty()) return o->objectName();
            return QStringLiteral("?");
        };
        for (auto *layout : window.findChildren<QLayout *>()) {
            const QMargins m = layout->contentsMargins();
            for (int v : {m.left(), m.top(), m.right(), m.bottom(), layout->spacing()})
                if (v >= 0 && !scale.contains(v))
                    bad << QStringLiteral("%1: layout value %2 is off the scale").arg(name(layout)).arg(v);
            // Bars and cards are surfaces: the same gap to all four edges.
            auto *surface = layout->parentWidget();
            if (surface && layout == surface->layout() && surface->autoFillBackground() == false
                && QStringList{"RedactBar", "TextBar", "SpotlightBar", "CropBar", "Toast",
                               "ColorPopover", "VideoPreview", "ZoomBar"}.contains(surface->objectName())
                && !(m.left() == m.top() && m.top() == m.right() && m.right() == m.bottom()))
                bad << QStringLiteral("%1: unequal padding %2/%3/%4/%5").arg(surface->objectName())
                           .arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom());
            QSet<int> heights;
            if (qobject_cast<QHBoxLayout *>(layout))
                for (int i = 0; i < layout->count(); ++i)
                    if (auto *b = qobject_cast<QAbstractButton *>(layout->itemAt(i)->widget()))
                        heights << b->height();
            if (heights.size() > 1)
                bad << QStringLiteral("%1: mixed control heights in one row").arg(name(layout));
        }
        for (auto *b : window.findChildren<QToolButton *>()) {
            const int px = QFontInfo(b->font()).pixelSize();
            if (px != theme::kFsCaption && px != theme::kFsUi)
                bad << QStringLiteral("%1: %2px text").arg(name(b)).arg(px);
            const bool labelled = !b->text().isEmpty() && !b->icon().isNull()
                && b->toolButtonStyle() != Qt::ToolButtonIconOnly;
            if (labelled && theme::iconInk(b->iconSize().height()) > px)
                bad << QStringLiteral("%1: %2px icon beside %3px text")
                           .arg(name(b)).arg(b->iconSize().height()).arg(px);
        }
        for (auto *l : window.findChildren<QLabel *>()) {
            const int px = QFontInfo(l->font()).pixelSize();
            if (!l->text().isEmpty() && px != theme::kFsCaption && px != theme::kFsUi)
                bad << QStringLiteral("%1: %2px text").arg(name(l)).arg(px);
        }
        bad.removeDuplicates();
        QVERIFY2(bad.isEmpty(), qPrintable(bad.join(QStringLiteral("\n   "))));
    }
    void spaceTogglesVideoButNeverAfterPanningOrTyping() {
        if (!have(QStringLiteral("ffmpeg"))) QSKIP("ffmpeg not available");
        QTemporaryDir dir;
        const QString path = dir.filePath("space.mp4");
        QVERIFY(runProcess("ffmpeg", {"-v", "error", "-f", "lavfi", "-i",
            "color=c=black:s=64x48:d=4:r=25", "-pix_fmt", "yuv420p", path}));
        MediaDocument doc; doc.kind = MediaKind::Video; doc.path = path;
        doc.video = {QSize(64, 48), 4000, 25.0};
        Config cfg; cfg.animations = false;
        EditorWindow window(doc, cfg, {});
        window.show();
        QTRY_VERIFY(window.findChild<QMediaPlayer *>());
        auto *player = window.findChild<QMediaPlayer *>();
        auto *canvas = window.findChild<Canvas *>();
        QTRY_VERIFY(player->isSeekable());
        QTest::keyClick(canvas, Qt::Key_Space);
        QTRY_COMPARE(player->playbackState(), QMediaPlayer::PlayingState);
        QTest::keyClick(canvas, Qt::Key_Space);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::keyPress(canvas, Qt::Key_Space);
        QKeyEvent repeat(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ", true);
        QApplication::sendEvent(canvas, &repeat);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(30, 30));
        QTest::mouseMove(canvas->viewport(), QPoint(45, 40));
        QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(45, 40));
        QTest::keyRelease(canvas, Qt::Key_Space);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QTest::keyPress(canvas, Qt::Key_Space);
        QEvent deactivate(QEvent::WindowDeactivate);
        QApplication::sendEvent(&window, &deactivate);
        QTest::keyRelease(canvas, Qt::Key_Space);
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
        QVERIFY(!canvas->spacePanActive());
        auto *tools = window.findChild<ToolController *>();
        tools->setTool(ToolType::Text);
        auto *text = static_cast<TextItem *>(tools->placeText(QPointF(5, 5)));
        text->setFocus();
        QTest::keyClicks(canvas, "a b");
        QCOMPARE(text->toPlainText(), QStringLiteral("a b"));
        QCOMPARE(player->playbackState(), QMediaPlayer::PausedState);
    }
    void usesPlatformWindowChrome() {
        QImage bg(100, 80, QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        Config cfg;
        cfg.animations = false;
        EditorWindow window(bg, cfg, {});
#ifdef Q_OS_WIN
        QVERIFY(!(window.windowFlags() & Qt::FramelessWindowHint));
        QVERIFY(window.windowFlags() & Qt::WindowMinMaxButtonsHint);
#else
        QVERIFY(window.windowFlags() & Qt::FramelessWindowHint);
#endif
        QVERIFY(window.windowFlags() & Qt::WindowStaysOnTopHint);
    }

    void initialViewUsesOneHundredPercentWhenImageFits() {
        QImage large(1000, 500, QImage::Format_ARGB32_Premultiplied); large.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow largeWindow(large, cfg, cli);
        largeWindow.show();
        QCoreApplication::processEvents();
        auto *largeCanvas = largeWindow.findChild<Canvas *>();
        QVERIFY(largeCanvas);
        QCOMPARE(largeCanvas->zoom(), 1.0);
        QVERIFY(largeCanvas->viewport()->width() >= large.width());
        QVERIFY(largeCanvas->viewport()->height() >= large.height());

        QImage small(64, 48, QImage::Format_ARGB32_Premultiplied); small.fill(Qt::white);
        EditorWindow smallWindow(small, cfg, cli);
        smallWindow.show();
        QCoreApplication::processEvents();
        auto *smallCanvas = smallWindow.findChild<Canvas *>();
        QVERIFY(smallCanvas);
        QCOMPARE(smallCanvas->zoom(), 1.0);
    }
    void smallImageKeepsEveryToolbarActionVisible() {
        QImage image(64, 48, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow window(image, cfg, cli);
        window.show();
        QCoreApplication::processEvents();
        for (const char *name : {"Undo", "Redo", "move", "arrow", "pen", "rect",
                                 "ellipse", "highlight", "text", "redact", "spotlight",
                                 "WidthS", "WidthM", "WidthL", "Swatch", "Save", "Copy",
                                 "SendToShelf", "Studio", "Theme"}) {
            auto *button = window.findChild<QToolButton *>(QString::fromLatin1(name));
            QVERIFY2(button, name);
            QVERIFY2(button->isVisible(), name);
        }
    }
    void bottomZoomControlsFollowTheCanvas() {
        QImage image(1400, 900, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, {});
        window.resize(1000, 600);
        window.show();
        QCoreApplication::processEvents();
        auto *canvas = window.findChild<Canvas *>();
        auto *fit = window.findChild<QToolButton *>("ZoomFit");
        auto *actual = window.findChild<QToolButton *>("ZoomActual");
        QVERIFY(canvas && fit && actual);
        actual->click();
        QCOMPARE(canvas->zoom(), 1.0);
        QCOMPARE(actual->text(), QStringLiteral("100%"));
        fit->click();
        QVERIFY(canvas->zoom() < 1.0);
        QCOMPARE(actual->text(), QStringLiteral("%1%").arg(qRound(canvas->zoom() * 100)));
        QVERIFY(window.windowTitle().contains(QStringLiteral("1400 × 900")));
    }
    void compactChromeKeepsControlsOutsideTheCanvas() {
        QImage image(1280, 720, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        Config cfg; cfg.animations = false;
        EditorWindow window(image, cfg, {});
        window.show();
        QCoreApplication::processEvents();
        auto *canvas = window.findChild<Canvas *>();
        QVERIFY(canvas);
        const QRect canvasRect(canvas->mapTo(&window, QPoint()), canvas->size());
        QVERIFY(window.width() - canvasRect.width() <= 60);
        QVERIFY(window.height() - canvasRect.height() <= 2 * theme::kBar);   // top bar and footer only
        auto *drag = window.findChild<DragPill *>();
        QVERIFY(drag);
        const QRect dragRect(drag->mapTo(&window, QPoint()), drag->size());
        QVERIFY(dragRect.top() > canvasRect.bottom());
        QVERIFY(window.rect().contains(dragRect));
        QVERIFY2(qAbs(window.rect().center().x() - dragRect.center().x()) <= 1,
            qPrintable(QStringLiteral("window center %1, drag center %2")
                .arg(window.rect().center().x()).arg(dragRect.center().x())));
        QList<QRect> controlRects;
        for (auto *button : window.findChildren<QToolButton *>()) {
            if (!button->isVisible()) continue;
            const QRect rect(button->mapTo(&window, QPoint()), button->size());
            QVERIFY2(window.rect().contains(rect), qPrintable(button->objectName()));
            QVERIFY2(!canvasRect.intersects(rect), qPrintable(button->objectName()));
            for (const QRect &previous : controlRects)
                QVERIFY2(!rect.intersects(previous), qPrintable(button->objectName()));
            controlRects.append(rect);
        }
    }
    void imageBackgroundUsesSmoothScaling() {
        QImage image(320, 180, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
        Config cfg;
        CliOptions cli;
        EditorWindow window(image, cfg, cli);
        auto *scene = window.findChild<QGraphicsScene *>();
        QVERIFY(scene);
        QGraphicsPixmapItem *background = nullptr;
        for (QGraphicsItem *item : scene->items())
            if ((background = dynamic_cast<QGraphicsPixmapItem *>(item))) break;
        QVERIFY(background);
        QCOMPARE(background->transformationMode(), Qt::SmoothTransformation);
    }
    void professionalKeyboardGestures() {
        QImage bg(1000,500,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && canvas && undo);
        auto *item = new RectItem(QRectF(20,20,30,30));
        item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
        scene->addItem(item);
        item->setSelected(true);

        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Right);
        QCOMPARE(item->pos(), QPointF(1,0));
        QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(item->pos(), QPointF(1,10));
        QCOMPARE(undo->count(), 2);

        QTest::keyClick(canvas, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(scene->selectedItems().size(), 1);
        QCOMPARE(scene->selectedItems().first()->pos(), QPointF(9,18));
        QCOMPARE(undo->count(), 3);

        QTest::keyPress(canvas, Qt::Key_Space);
        QVERIFY(canvas->spacePanActive());
        QTest::keyRelease(canvas, Qt::Key_Space);
        QVERIFY(!canvas->spacePanActive());

        QTest::keyClick(canvas, Qt::Key_Plus);
        QVERIFY(canvas->zoom() > 1.0);
        QTest::keyClick(canvas, Qt::Key_1);
        QCOMPARE(canvas->zoom(), 1.0);
    }
    void escapeCancelsActiveGestureBeforeClosing() {
        QImage bg(100,80,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *tools = w.findChild<ToolController *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && tools && undo);
        tools->setTool(ToolType::Rect);
        tools->begin({10,10});
        QCOMPARE(scene->items().size(), 2); // background + preview

        QTest::keyClick(&w, Qt::Key_Escape);
        QCOMPARE(scene->items().size(), 1);
        QCOMPARE(undo->count(), 0);
        QVERIFY(w.isVisible());
    }
    void deletingMultipleItemsIsOneUndoStep() {
        QImage bg(100,80,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && undo);
        for (int x : {10, 50}) {
            auto *item = new RectItem(QRectF(x,10,20,20));
            item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            scene->addItem(item);
            item->setSelected(true);
        }

        QTest::keyClick(&w, Qt::Key_Delete);
        QCOMPARE(scene->items().size(), 1);
        QCOMPARE(undo->count(), 1);
        undo->undo();
        QCOMPARE(scene->items().size(), 3);
    }
    void hasRedactBarAndToast() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.findChild<RedactBar *>() != nullptr);   // mode-bar constructed
        QVERIFY(w.findChild<Toast *>() != nullptr);        // toast constructed
        QVERIFY(w.findChild<RedactBar *>()->isHidden());   // hidden until a redact is selected
    }
    void selectedTextShowsContextBar() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *bar = w.findChild<TextBar *>();
        QVERIFY(scene && bar);
        QVERIFY(bar->isHidden());
        auto *text = new TextItem(QStringLiteral("Label"), Qt::red, 18);
        text->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(text);
        text->setSelected(true);
        QVERIFY(!bar->isHidden());
    }
    void textContextBarDoesNotCoverSelectedText() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        w.resize(w.minimumSize());
        w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *bar = w.findChild<TextBar *>();
        QVERIFY(scene && canvas && bar);
        auto *text = new TextItem(QStringLiteral("Move me"), Qt::red, 18);
        text->setPos(canvas->mapToScene(QPoint(160,2)));
        text->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(text);
        text->setSelected(true);
        QCoreApplication::processEvents();

        const QRect itemRect(
            canvas->mapFromScene(text->sceneBoundingRect().topLeft()),
            canvas->mapFromScene(text->sceneBoundingRect().bottomRight()));
        QVERIFY(!bar->geometry().intersects(itemRect.normalized()));
    }
    void textContextControlsAreSelfExplanatory() {
        TextBar bar;
        // Every control is a grid icon, letterforms included: no text, no
        // second rendering path to drift out of step with the rest.
        const QStringList names = {
            QStringLiteral("TextSize14"), QStringLiteral("TextSize20"),
            QStringLiteral("TextSize28"), QStringLiteral("TextBold"),
            QStringLiteral("TextAlignLeft"), QStringLiteral("TextAlignCenter"),
            QStringLiteral("TextAlignRight"), QStringLiteral("TextFill")};
        for (const QString &name : names) {
            auto *button = bar.findChild<QToolButton *>(name);
            QVERIFY2(button, qPrintable(name));
            QVERIFY2(!button->toolTip().isEmpty(), qPrintable(name));
            QCOMPARE(button->accessibleName(), button->toolTip());
            QVERIFY2(button->text().isEmpty(), qPrintable(name));
            QVERIFY2(!button->icon().isNull(), qPrintable(name));
        }
    }
    void spotlightContextChangesAreUndoable() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *undo = w.findChild<QUndoStack *>();
        auto *bar = w.findChild<SpotlightBar *>();
        QVERIFY(scene && undo && bar);
        auto *spot = new SpotlightItem(QRectF(40,30,100,80), bg.size());
        spot->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(spot); spot->setSelected(true);
        QVERIFY(!bar->isHidden());

        emit bar->shapeChosen(SpotlightShape::Ellipse);
        QCOMPARE(spot->spotlightShape(), SpotlightShape::Ellipse);
        QCOMPARE(undo->count(), 1);
        undo->undo();
        QCOMPARE(spot->spotlightShape(), SpotlightShape::RoundedRect);

        emit bar->intensityChosen(3);
        QCOMPARE(spot->intensity(), 3);
        QCOMPARE(undo->count(), 1); // new edit replaces the abandoned redo branch
        undo->undo();
        QCOMPARE(spot->intensity(), 2);
    }
    void spotlightContextBarFollowsItemMove() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli); w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *bar = w.findChild<SpotlightBar *>();
        auto *spot = new SpotlightItem(QRectF(60,80,100,70), bg.size());
        spot->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(spot); spot->setSelected(true);
        QTest::qWait(20);
        const QPoint before = bar->pos();
        spot->moveBy(30, 15);
        QTest::qWait(20);
        const QPoint delta = canvas->mapFromScene(QPointF(30,15))
            - canvas->mapFromScene(QPointF(0,0));
        QCOMPARE(bar->pos(), before + delta);
    }
    void redactContextBarFollowsItemMove() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli); w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *bar = w.findChild<RedactBar *>();
        auto *redact = new RedactItem(RedactMode::Blacken, bg, QRectF(60,80,100,70));
        redact->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
        scene->addItem(redact); redact->setSelected(true);
        QTest::qWait(20);
        const QPoint before = bar->pos();
        redact->moveBy(30, 15);
        QTest::qWait(20);
        const QPoint delta = canvas->mapFromScene(QPointF(30,15))
            - canvas->mapFromScene(QPointF(0,0));
        QCOMPARE(bar->pos(), before + delta);
    }
    void themeButtonSwitchesAndPersists() {
        const QPalette oldPalette = QApplication::palette();
        const QString oldStyle = qApp->styleSheet();
        QApplication::setPalette(theme::palette(false));
        qApp->setStyleSheet(theme::styleSheet(false));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Config cfg; cfg.animations = false; cfg.theme = ThemeMode::Light;
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        QImage bg(100,80,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        EditorWindow w(bg, cfg, cli);
        auto *button = w.findChild<QToolButton *>(QStringLiteral("Theme"));
        QVERIFY(button);

        button->click();
        QCOMPARE(QApplication::palette().color(QPalette::Window), theme::tone(theme::Tone::Bg, true));
        QSettings settings(cli.configPath, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("eddy/theme")).toString(), QStringLiteral("dark"));
        button->click();
        QCOMPARE(QApplication::palette().color(QPalette::Window), theme::tone(theme::Tone::Bg, false));

        QApplication::setPalette(oldPalette);
        qApp->setStyleSheet(oldStyle);
    }
    void textKeyboardCommitAndCancelUseSingleTransaction() {
        QImage bg(300,180,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; cfg.animations = false; CliOptions cli;
        EditorWindow w(bg, cfg, cli); w.show();
        auto *scene = w.findChild<QGraphicsScene *>();
        auto *canvas = w.findChild<Canvas *>();
        auto *tools = w.findChild<ToolController *>();
        auto *undo = w.findChild<QUndoStack *>();
        QVERIFY(scene && canvas && tools && undo);

        auto *committed = dynamic_cast<TextItem *>(tools->placeText({30,30}));
        QVERIFY(committed);
        committed->setPlainText(QStringLiteral("Two\nlines"));
        QTest::keyClick(canvas, Qt::Key_Return, Qt::ControlModifier);
        QCOMPARE(tools->editingText(), nullptr);
        QCOMPARE(undo->count(), 1);
        QVERIFY(committed->scene() == scene);
        QVERIFY(!committed->hasFocus());
        QVERIFY(scene->selectedItems().isEmpty());
        QVERIFY(w.findChild<TextBar *>()->isHidden());
        QCOMPARE(scene->focusItem(), nullptr);
        QCOMPARE(committed->textInteractionFlags(), Qt::NoTextInteraction);
        QCOMPARE(committed->toPlainText(), QStringLiteral("Two\nlines"));

        auto *cancelled = dynamic_cast<TextItem *>(tools->placeText({60,60}));
        QVERIFY(cancelled);
        cancelled->setPlainText(QStringLiteral("Discard me"));
        QTest::keyClick(canvas, Qt::Key_Escape);
        QCOMPARE(tools->editingText(), nullptr);
        QCOMPARE(undo->count(), 1);
        int textCount = 0;
        for (QGraphicsItem *item : scene->items())
            if (dynamic_cast<TextItem *>(item)) ++textCount;
        QCOMPARE(textCount, 1);
    }
    void buildsAndExportsCompositeSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QImage out = w.exportComposite();
        QCOMPARE(out.size(), QSize(64,48));
    }
    void imageBlurWorksThroughEditorExport() {
        QImage bg(96, 64, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < bg.height(); ++y)
            for (int x = 0; x < bg.width(); ++x)
                bg.setPixelColor(x, y, ((x / 2 + y / 2) % 2) ? Qt::white : Qt::black);
        Config cfg; cfg.animations = false;
        EditorWindow window(bg, cfg, {});
        auto *tools = window.findChild<ToolController *>();
        QVERIFY(tools);
        tools->setTool(ToolType::Redact);
        tools->begin({16, 12});
        tools->finish({80, 52});

        const QImage out = window.exportComposite();
        QCOMPARE(out.pixelColor(4, 4), bg.pixelColor(4, 4));
        QVERIFY(out.pixelColor(48, 32) != bg.pixelColor(48, 32));
        QVERIFY(qAbs(out.pixelColor(48, 32).red() - out.pixelColor(49, 32).red()) < 30);
    }
    void exportKeepsAnnotationSelected() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *scene = w.findChild<QGraphicsScene *>();
        QVERIFY(scene);
        auto *item = scene->addRect(QRectF(4, 4, 12, 12));
        item->setFlag(QGraphicsItem::ItemIsSelectable);
        item->setSelected(true);

        w.exportComposite();

        QVERIFY(item->isSelected());
    }
    void hasMinimumSize() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        QVERIFY(w.minimumWidth() >= 760);
        QVERIFY(w.minimumHeight() > 0);
    }
    void hasDragOutPill() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *pill = w.findChild<DragPill *>();
        QVERIFY(pill != nullptr);            // pill exists and is visible (not hidden like the bar/toast)
        QVERIFY(!pill->isHidden());
    }
    void hasSendToShelfAction() {
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white);
        Config cfg; CliOptions cli;
        EditorWindow w(bg, cfg, cli);
        auto *button = w.findChild<QToolButton *>(QStringLiteral("SendToShelf"));
        QVERIFY(button != nullptr);
        QVERIFY(!button->isHidden());
    }
    void saveRoutingUsesExplicitCardConfigShelfPriority() {
        CliOptions cli;
        Config cfg;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::Shelf);

        cfg.saveDir = QStringLiteral("/tmp/configured");
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ConfigDirectory);

        cli.boltsnapCardId = 42;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::BoltsnapCard);

        cli.output.toFile = true;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ExplicitOutput);

        cli.output.toFile = false;
        cli.output.toStdout = true;
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ExplicitOutput);

        cli.output.toStdout = false;
        cli.output.saveDir = QStringLiteral("/tmp/eddy-test");
        QCOMPARE(saveRoute(cli, cfg), SaveRoute::ExplicitOutput);
    }
    void configuredSaveDirectoryWritesImage() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage bg(64,48,QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        Config cfg;
        cfg.saveDir = dir.path();
        cfg.copyOnSave = false;
        CliOptions cli;
        EditorWindow w(bg, cfg, cli);

        w.save();

        const QStringList files = QDir(dir.path()).entryList(
            {QStringLiteral("eddy-*.png")}, QDir::Files);
        QCOMPARE(files.size(), 1);
    }
    void studioIsOffByDefault() {
        QImage img(200, 100, QImage::Format_ARGB32); img.fill(Qt::red);
        Config cfg; cfg.animations = false;
        EditorWindow w(img, cfg, {});
        QVERIFY(!w.studioStyle().active());
        auto *button = w.findChild<QToolButton *>(QStringLiteral("Studio"));
        QVERIFY(button && !button->isChecked());
        QCOMPARE(w.exportComposite().size(), QSize(200, 100));
    }
    void studioFramesTheImageExport() {
        QImage img(200, 100, QImage::Format_ARGB32); img.fill(Qt::red);
        Config cfg; cfg.animations = false;
        EditorWindow w(img, cfg, {});
        StudioStyle style;
        style.background = StudioStyle::Background::Color;
        style.color = QColor(0, 0, 255);
        style.padding = 10; style.radius = 0; style.shadow = 0;
        w.setStudioStyle(style);
        QVERIFY(w.findChild<QToolButton *>(QStringLiteral("Studio"))->isChecked());
        QImage out = w.exportComposite();
        QCOMPARE(out.size(), QSize(220, 120));
        QCOMPARE(out.pixelColor(2, 2), QColor(0, 0, 255));
        QCOMPARE(out.pixelColor(110, 60), QColor(255, 0, 0));
    }
    void studioSessionIsOneUndoStepAndRemembersTheStyle() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        CliOptions cli; cli.configPath = dir.filePath(QStringLiteral("config"));
        QImage img(200, 100, QImage::Format_ARGB32); img.fill(Qt::red);
        Config cfg; cfg.animations = false;
        StudioStyle chosen;
        {
            EditorWindow w(img, cfg, cli);
            auto *undo = w.findChild<QUndoStack *>();
            QVERIFY(undo);
            w.openStudio();
            QVERIFY2(w.studioStyle().active(), "opening Studio switches it on with the last style");
            auto *popover = w.findChild<StudioPopover *>();
            QVERIFY(popover);
            // The output size follows the style: 200x100 grows by the padding.
            const auto *size = popover->findChild<QLabel *>(QStringLiteral("StudioSize"));
            QVERIFY(size);
            const QSize out = studioLayout(QSize(200, 100), w.studioStyle()).output;
            QCOMPARE(size->text(), QStringLiteral("%1 × %2").arg(out.width()).arg(out.height()));
            // Several slider moves inside one popover session.
            for (auto *slider : popover->findChildren<QSlider *>()) slider->setValue(30);
            chosen = w.studioStyle();
            const QSize grown = studioLayout(QSize(200, 100), chosen).output;
            QCOMPARE(size->text(), QStringLiteral("%1 × %2").arg(grown.width()).arg(grown.height()));
            popover->close();
            QTRY_VERIFY(!w.findChild<StudioPopover *>());
            QCOMPARE(undo->count(), 1);
            undo->undo();
            QVERIFY(!w.studioStyle().active());
            undo->redo();
            QCOMPARE(w.studioStyle(), chosen);
        }
        // A new document starts off, but switching on restores the last style.
        EditorWindow next(img, cfg, cli);
        QVERIFY(!next.studioStyle().active());
        next.openStudio();
        QCOMPARE(next.studioStyle().padding, chosen.padding);
        QCOMPARE(next.studioStyle().shadow, chosen.shadow);
        QCOMPARE(next.studioStyle().color, chosen.color);
    }
};
QTEST_MAIN(TestEditorWindow)
#include "test_editorwindow.moc"

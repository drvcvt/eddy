# Professional Gestures Implementation Plan

> **For agentic workers:** Implement this plan task-by-task — one task per commit, run each task's test before moving on. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add consistent professional creation, resize, selection, duplication, navigation, cancellation, cursor, and dark/light-theme behavior to Eddy's existing Qt annotation paths.

**Architecture:** Extend the existing `ToolController`, `Canvas`, `SelectionHandles`, `EditorWindow`, and `QUndoStack` paths. Geometry is recomputed from the gesture's original state on every pointer update, retained items clone themselves for duplication, and application palette/QSS tokens resolve once at startup.

**Tech Stack:** C++20, Qt 6 Widgets/Test/Svg, CMake, CTest

## Global Constraints

- Preserve all existing uncommitted work and never stage dirty source files wholesale.
- Use no new dependency, UI toolkit, input abstraction, sidebar, or object-snapping system.
- Every complete user gesture creates exactly one undo entry.
- New UI follows mt-ui grayscale tokens, 14/9 px radii, brightness-only depth, and no structural borders or shadows.
- Both dark and light palettes must render and be inspected.

---

### Task 1: Modifier-aware creation and cancellation

**Files:**
- Modify: `src/toolcontroller.h`
- Modify: `src/toolcontroller.cpp`
- Modify: `src/canvas.cpp`
- Test: `tests/test_toolcontroller.cpp`

**Interfaces:**
- Consumes: the existing `ToolController::begin/update/finish` gesture lifecycle.
- Produces: `update(const QPointF &, Qt::KeyboardModifiers)`, `finish(const QPointF &, Qt::KeyboardModifiers)`, and `cancelActive() -> bool`.

- [ ] **Step 1: Write failing geometry and cancellation tests**

```cpp
void shiftCreatesSquareAndSnapsArrow() {
    QGraphicsScene scene; QUndoStack undo;
    ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
    tc.setAnimationsEnabled(false);
    tc.setTool(ToolType::Rect);
    tc.begin({10,10}); tc.finish({40,25}, Qt::ShiftModifier);
    auto *rect = dynamic_cast<RectItem *>(scene.items().first());
    QCOMPARE(rect->rect().size(), QSizeF(30,30));

    tc.setTool(ToolType::Arrow);
    tc.begin({0,0}); tc.finish({30,10}, Qt::ShiftModifier);
    auto *arrow = dynamic_cast<ArrowItem *>(scene.items().first());
    QVERIFY(qAbs(arrow->end().y()) < 0.001);
}

void altCreatesFromCenterAndCancelDoesNotAddUndo() {
    QGraphicsScene scene; QUndoStack undo;
    ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
    tc.setTool(ToolType::Ellipse);
    tc.begin({50,50}); tc.update({70,60}, Qt::AltModifier);
    auto *ellipse = dynamic_cast<EllipseItem *>(scene.items().first());
    QCOMPARE(ellipse->rect(), QRectF(30,40,40,20));
    QVERIFY(tc.cancelActive());
    QCOMPARE(scene.items().size(), 0);
    QCOMPARE(undo.count(), 0);
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build build --target test_toolcontroller -j2 && QT_QPA_PLATFORM=offscreen ./build/test_toolcontroller shiftCreatesSquareAndSnapsArrow altCreatesFromCenterAndCancelDoesNotAddUndo`

Expected: compilation fails because the modifier overloads and `cancelActive` do not exist.

- [ ] **Step 3: Implement geometry from the original press point**

```cpp
static QPointF constrainedEnd(const QPointF &start, QPointF end,
                              Qt::KeyboardModifiers modifiers, bool arrow) {
    QPointF d = end - start;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        if (arrow) {
            const qreal length = std::hypot(d.x(), d.y());
            constexpr qreal step = std::numbers::pi_v<qreal> / 4.0;
            const qreal angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
            d = {length * std::cos(angle), length * std::sin(angle)};
        } else {
            const qreal side = qMax(qAbs(d.x()), qAbs(d.y()));
            d = {std::copysign(side, d.x()), std::copysign(side, d.y())};
        }
    }
    return start + d;
}

static QRectF creationRect(const QPointF &start, QPointF end,
                           Qt::KeyboardModifiers modifiers) {
    end = constrainedEnd(start, end, modifiers, false);
    const QPointF d = end - start;
    return (modifiers.testFlag(Qt::AltModifier)
        ? QRectF(start - d, start + d)
        : QRectF(start, end)).normalized();
}
```

Use `creationRect` for Rectangle, Ellipse, Highlight, and Redact. Use `constrainedEnd(..., true)` for Arrow. Pen ignores modifiers. `cancelActive` removes and deletes only the live preview and returns whether it cancelled anything. `Canvas` forwards `QMouseEvent::modifiers()`.

- [ ] **Step 4: Run the focused test and verify GREEN**

Run the command from Step 2. Expected: both tests pass.

- [ ] **Step 5: Review without committing dirty source files**

Run: `git diff --check -- src/toolcontroller.h src/toolcontroller.cpp src/canvas.cpp tests/test_toolcontroller.cpp`

Expected: no whitespace errors. Do not stage these already-dirty files.

### Task 2: Modifier-aware selection handles

**Files:**
- Modify: `src/selectionhandles.h`
- Modify: `src/selectionhandles.cpp`
- Test: `tests/test_selectionhandles.cpp`

**Interfaces:**
- Consumes: the original rect/arrow captured on handle press and `QGraphicsSceneMouseEvent::modifiers()`.
- Produces: public pure helpers `resizedRect(...)` and `snappedArrowEnd(...)` used by handles and tests.

- [ ] **Step 1: Write failing pure geometry tests**

```cpp
void resizeModifiersUseOriginalGeometry() {
    const QRectF before(10,20,80,40);
    QCOMPARE(resizedRect(before, 4, QPointF(130,90), Qt::ShiftModifier),
             QRectF(10,20,120,60));
    QCOMPARE(resizedRect(before, 4, QPointF(110,80), Qt::AltModifier),
             QRectF(-10,0,120,80));
}

void arrowResizeShiftSnapsToFortyFiveDegrees() {
    const QPointF end = snappedArrowEnd({0,0}, {20,13}, Qt::ShiftModifier);
    QVERIFY(qAbs(end.x() - end.y()) < 0.001);
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run: `cmake --build build --target test_selectionhandles -j2 && QT_QPA_PLATFORM=offscreen ./build/test_selectionhandles resizeModifiersUseOriginalGeometry arrowResizeShiftSnapsToFortyFiveDegrees`

Expected: compilation fails because the helpers do not exist.

- [ ] **Step 3: Implement resizing from captured state**

```cpp
QRectF resizedRect(const QRectF &before, int role, const QPointF &pointer,
                   Qt::KeyboardModifiers modifiers);
QPointF snappedArrowEnd(const QPointF &fixed, const QPointF &pointer,
                       Qt::KeyboardModifiers modifiers);
```

Corner roles move their corner against the captured opposite corner. `Shift` preserves `before.width()/before.height()`. `Alt` mirrors the moved edge around the captured center. Edge roles apply only their axis unless `Shift` needs the second axis for aspect preservation. `Shift+Alt` combines both. Arrow endpoint handles use the other captured endpoint as the fixed point and snap to 45 degrees. `HandleItem::applyResize` always starts from `m_beforeRect`, `m_s0`, and `m_e0`; it never accumulates the prior mouse move.

- [ ] **Step 4: Run focused tests and verify GREEN**

Run the command from Step 2. Expected: both tests and existing handle tests pass.

- [ ] **Step 5: Review without committing dirty source files**

Run: `git diff --check -- src/selectionhandles.h src/selectionhandles.cpp tests/test_selectionhandles.cpp`.

### Task 3: Duplication, multi-selection, keyboard movement, pan, zoom, and Escape

**Files:**
- Modify: `src/items/annotationitem.h`
- Modify: `src/items/arrowitem.{h,cpp}`
- Modify: `src/items/rectitem.{h,cpp}`
- Modify: `src/items/ellipseitem.{h,cpp}`
- Modify: `src/items/highlightitem.{h,cpp}`
- Modify: `src/items/redactitem.{h,cpp}`
- Modify: `src/items/penpathitem.{h,cpp}`
- Modify: `src/items/textitem.{h,cpp}`
- Modify: `src/toolcontroller.{h,cpp}`
- Modify: `src/canvas.{h,cpp}`
- Modify: `src/editorwindow.{h,cpp}`
- Test: `tests/test_items.cpp`
- Test: `tests/test_toolcontroller.cpp`
- Test: `tests/test_canvas.cpp`
- Test: `tests/test_editorwindow.cpp`

**Interfaces:**
- Consumes: existing retained item state, `AddItemCommand`, `MoveItemsCommand`, and native Qt item selection.
- Produces: `clone()`, `duplicateSelection(offset)`, `nudgeSelection(delta)`, `setSpacePan`, `fitMedia`, and `resetZoom`.

- [ ] **Step 1: Write failing clone, one-command duplicate, nudge, pan, zoom, and Escape tests**

```cpp
void duplicateSelectionIsOneUndoStep() {
    QGraphicsScene scene; QUndoStack undo;
    ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
    auto *rect = new RectItem(QRectF(0,0,20,10));
    rect->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    scene.addItem(rect); rect->setSelected(true);
    QVERIFY(tc.duplicateSelection({8,8}));
    QCOMPARE(scene.items().size(), 2);
    QCOMPARE(undo.count(), 1);
    undo.undo(); QCOMPARE(scene.items().size(), 1);
}

void nudgeSelectionUsesOneUndoStep() {
    QGraphicsScene scene; QUndoStack undo;
    ToolController tc(&scene, &undo, QImage(100,100,QImage::Format_ARGB32_Premultiplied));
    auto *rect = new RectItem(QRectF(0,0,20,10));
    rect->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    scene.addItem(rect); rect->setSelected(true);
    QVERIFY(tc.nudgeSelection({10,0}));
    QCOMPARE(rect->pos(), QPointF(10,0));
    QCOMPARE(undo.count(), 1);
}
```

Add Canvas tests asserting left-drag pans while `setSpacePan(true)`, `resetZoom()` returns `zoom()` to `1.0`, and `fitMedia()` fits the scene rect. Add an EditorWindow test asserting the first Escape cancels an active drawing while the window remains open.

- [ ] **Step 2: Run focused tests and verify RED**

Run: `cmake --build build --target test_items test_toolcontroller test_canvas test_editorwindow -j2`

Expected: compilation fails on the missing clone, duplication, nudge, pan, and zoom APIs.

- [ ] **Step 3: Add the minimum retained-item cloning contract**

```cpp
class AnnotationItem : public QGraphicsItem {
public:
    virtual AnnotationItem *clone() const = 0;
};

class TextItem : public QGraphicsTextItem {
public:
    TextItem *clone() const;
};
```

Each concrete item constructs the same local geometry/content and copies its complete style state. Redact also copies mode, OCR rectangles, and detection state; Pen copies its points; Text copies text, color, font, width, alignment, and interaction flags. `ToolController` copies common position/transform/z/opacity/flags, adds all clones inside one `QUndoStack::beginMacro("duplicate")`/`endMacro()`, selects only the clones, and returns false for unsupported selected items.

- [ ] **Step 4: Add selection and navigation behavior through existing controllers**

```cpp
bool ToolController::nudgeSelection(const QPointF &delta) {
    const auto items = movableSelection();
    if (items.isEmpty()) return false;
    QList<QPointF> before, after;
    for (auto *item : items) { before << item->pos(); after << item->pos() + delta; }
    m_undo->push(new MoveItemsCommand(items, before, after));
    return true;
}
```

`EditorWindow` maps arrows to 1 px or 10 px with Shift, `Ctrl+D` to an 8 px duplicate, `0`/`1`/`+`/`-` to Canvas navigation, and Escape to active text/eyedropper/drawing/pan before close. `Canvas` uses left drag for temporary Space-pan, translates Shift-click to native additive/toggle selection, and duplicates the selected set before an Alt-drag. Pan activation updates open/closed-hand cursors without changing the selected tool.

- [ ] **Step 5: Run focused tests and the complete suite**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: all tests pass.

### Task 4: System/dark/light themes and visual verification

**Files:**
- Modify: `src/config.{h,cpp}`
- Modify: `src/theme.{h,cpp}`
- Modify: `src/main.cpp`
- Modify: `src/toolbar.cpp`
- Modify: `src/canvas.cpp`
- Modify: `src/selectionhandles.cpp`
- Modify: `resources/eddy.qss`
- Modify: `tools/eddy_preview.cpp`
- Test: `tests/test_config.cpp`
- Test: `tests/test_theme.cpp`

**Interfaces:**
- Consumes: system `QPalette`, existing QSS resource, and `[eddy]` config.
- Produces: `ThemeMode { System, Dark, Light }`, `resolveDark`, `palette`, and token-expanded `styleSheet`.

- [ ] **Step 1: Write failing config and palette tests**

```cpp
void themeDefaultsToSystemAndParsesOverrides() {
    QCOMPARE(loadConfig("/nonexistent/eddy/config").theme, ThemeMode::System);
    QTemporaryFile f; QVERIFY(f.open());
    { QTextStream out(&f); out << "[eddy]\ntheme=light\n"; } f.flush();
    QCOMPARE(loadConfig(f.fileName()).theme, ThemeMode::Light);
}

void lightPaletteUsesApprovedTokens() {
    const QPalette p = theme::palette(false);
    QCOMPARE(p.color(QPalette::Window), QColor("#FAFAFA"));
    QCOMPARE(p.color(QPalette::Base), QColor("#F1F1F1"));
    QCOMPARE(p.color(QPalette::WindowText), QColor("#1A1A1A"));
}
```

- [ ] **Step 2: Run tests and verify RED**

Run: `cmake --build build --target test_config test_theme -j2`

Expected: compilation fails on missing theme types and functions.

- [ ] **Step 3: Implement token resolution once at startup**

```cpp
enum class ThemeMode { System, Dark, Light };
QPalette palette(bool dark);
bool resolveDark(ThemeMode mode, const QPalette &systemPalette);
QString styleSheet(bool dark);
```

Define `ThemeMode` in `theme.h`; `config.h` includes it and stores `ThemeMode theme = ThemeMode::System`. `styleSheet` loads `:/eddy.qss` and substitutes semantic placeholders for `bg`, `raise1`, `raise2`, `raise3`, `fg`, `sub`, `faint`, `chip-on`, and `chip-on-fg`. `main.cpp` preserves the original system palette, loads config, resolves the mode, then applies palette and QSS before constructing `EditorWindow`. Icons and selection handles read semantic colors from the active application palette instead of dark constants.

- [ ] **Step 4: Run all automated checks**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: all tests pass.

- [ ] **Step 5: Render and inspect both themes**

Run:

```bash
QT_QPA_PLATFORM=offscreen ./build/eddy_preview /tmp/eddy-dark.png dark
QT_QPA_PLATFORM=offscreen ./build/eddy_preview /tmp/eddy-light.png light
```

Inspect both real renders for grayscale-only application chrome, no borders/shadows, 14/9 px radii, readable controls, and visible selection handles. Do not commit already-dirty source files; report the exact diff and checks instead.

# Eddy Project Review Implementation Plan

> **For agentic workers:** Implement this plan task-by-task, run each task's focused check before moving on, and do not commit over the user's existing dirty worktree.

**Goal:** Review Eddy end to end, directly fix bounded correctness and UI-style issues, and stop before theme architecture or other product decisions.

**Architecture:** Keep fixes in the existing Qt Widgets paths: privacy behavior in `RedactItem`, toolbar state in `Toolbar`, and visual tokens in the existing QSS. Do not add dependencies or new abstraction layers.

**Tech Stack:** C++20, Qt 6 Widgets/Test, CMake/CTest.

## Global Constraints

- Preserve the user's existing uncommitted IPC/CLI/editor changes.
- Follow `mt-ui-style`: neutral grayscale, no structural borders/shadows, 14/9 px radii, brightness-only depth, dark and light required for eventual compliance.
- Apply only bounded fixes now; report larger decisions and wait.

---

### Task 1: Fail-safe empty OCR redaction

**Files:**
- Modify: `tests/test_items.cpp`
- Modify: `src/items/redactitem.cpp`

**Interfaces:**
- Consumes: `RedactItem::setTextRects`, `RedactItem::setDetecting`
- Produces: empty completed OCR results still cover the selected region

- [ ] **Step 1: Add the failing regression test**

```cpp
void emptyOcrResultKeepsRegionCovered() {
    QGraphicsScene scene(0,0,80,80);
    QImage src(80,80,QImage::Format_ARGB32); src.fill(Qt::white);
    auto *r = new RedactItem(RedactMode::OcrBlacken, src, QRectF(10,10,60,60));
    r->setTextRects({});
    r->setDetecting(false);
    scene.addItem(r);
    QCOMPARE(renderScene(scene, QSize(80,80)).pixelColor(40,40).alpha(), 255);
}
```

- [ ] **Step 2: Verify red**

Run: `cmake --build build --target test_items -j2 && QT_QPA_PLATFORM=offscreen ./build/test_items emptyOcrResultKeepsRegionCovered`

Expected: FAIL because the rendered pixel is transparent.

- [ ] **Step 3: Implement the shared fail-safe**

```cpp
if (!isOcr(m_mode) || m_detecting || m_textRects.isEmpty()) return { m_region };
```

- [ ] **Step 4: Verify green**

Run the focused command again; expected: PASS.

### Task 2: Remove non-compliant toolbar ornament

**Files:**
- Modify: `tests/test_toolbar.cpp`
- Modify: `src/toolbar.h`
- Modify: `src/toolbar.cpp`
- Modify: `resources/eddy.qss`

**Interfaces:**
- Consumes: native `QToolButton::checked` state
- Produces: one static inverted active squircle without separators or a sliding overlay

- [ ] **Step 1: Change the toolbar test to require native checked state and no overlay**

```cpp
void syncToolUsesCheckedStateWithoutOverlay() {
    Toolbar tb;
    tb.syncTool(ToolType::Rect);
    QVERIFY(tb.findChild<QWidget *>("Pill") == nullptr);
}
```

- [ ] **Step 2: Verify red**

Run: `cmake --build build --target test_toolbar -j2 && QT_QPA_PLATFORM=offscreen ./build/test_toolbar syncToolUsesCheckedStateWithoutOverlay`

Expected: FAIL because `Pill` exists.

- [ ] **Step 3: Delete the overlay animation and separator frames**

Remove `m_pill`, `m_pillAnim`, `m_active`, `movePillTo`, the three `QFrame::VLine` widgets, and their now-unused includes. Keep `syncTool` as `b->setChecked(true)`.

- [ ] **Step 4: Align the existing dark QSS with the approved tokens**

Use `#121212/#1A1A1A/#222222/#2B2B2B/#ECECEC/#969696`, `Geist` then `Inter`, 14 px panel radii, 9 px control radii, and no structural borders. Use the inverted checked state only for active tools.

- [ ] **Step 5: Verify green**

Run the focused toolbar test; expected: PASS.

### Task 3: Verify and report the decision boundary

**Files:**
- Modify only if a focused regression requires it.

**Interfaces:**
- Consumes: built `eddy_preview`, complete CTest suite
- Produces: evidence-backed review plus explicit larger options

- [ ] **Step 1: Build and run all tests**

Run: `cmake --build build -j2 && ctest --test-dir build --output-on-failure`

Expected: all tests pass, or pre-existing failures are identified separately.

- [ ] **Step 2: Render and inspect the real UI**

Run: `QT_QPA_PLATFORM=offscreen ./build/eddy_preview /tmp/eddy-dark.png`

Inspect the PNG against the no-border/no-shadow/radius/type hierarchy checklist. Record that a compliant light rendering is blocked by the current dark-only architecture rather than fabricating one.

- [ ] **Step 3: Report larger choices without implementing them**

Offer a minimal theme architecture choice (system default plus persisted toolbar toggle is recommended), and separately list any larger correctness/product changes found during the review.

---

### Task 4: Implement approved save, undo, and reliability follow-ups

**Files:**
- Modify: `tests/test_config.cpp`, `tests/test_editorwindow.cpp`, `tests/test_canvas.cpp`, `tests/test_boltsnapipc.cpp`
- Modify: `src/config.cpp`, `src/config.h`, `src/editorwindow.cpp`, `src/editorwindow.h`
- Modify: `src/canvas.cpp`, `src/toolcontroller.cpp`, `src/toolcontroller.h`, `src/undocommands.cpp`, `src/undocommands.h`
- Modify: `src/boltsnapipc.cpp`, `src/dragpill.cpp`, `src/videoexporter.cpp`

**Interfaces:**
- Produces: explicit CLI output > Boltsnap card replacement > configured save directory > shelf return
- Produces: one undo entry per native annotation drag
- Produces: bounded Boltsnap socket writes and cleanup of Eddy-owned temporary media

- [x] **Step 1: Add and run failing routing, move-undo, and IPC timeout regressions**
- [x] **Step 2: Centralize the save route and apply it to images and videos**
- [x] **Step 3: Capture native move drags as one undo command**
- [x] **Step 4: Add socket send timeout/no-SIGPIPE behavior and bounded temp cleanup**
- [x] **Step 5: Run focused tests, the full CTest suite, and inspect the final diff**

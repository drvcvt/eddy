# Video Workflow Reliability Implementation Plan

> **For agentic workers:** Implement this plan task-by-task — one task per commit, run each task's test before moving on. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make edited-video delivery reliable, non-blocking, cache-correct, and visually consistent without changing Eddy's RAM-backed fast-path.

**Architecture:** Keep the existing background export cache. Add framed ACK handling only to video IPC, carry pending copy/save intent through cache completion, invalidate the cache when asynchronous OCR changes the overlay, and reuse the cache for file saves. Boltsnap remains the final authority for accepting ownership.

**Tech Stack:** C++20/Qt 6 Widgets and Qt Test in Eddy; Rust/std Unix sockets in Boltsnap; existing framed JSON IPC.

## Global Constraints

- Preserve direct source-path delivery for unedited videos.
- Keep temporary video storage below the existing 2 GiB aggregate limit.
- Add no dependencies or new abstraction layer.
- Preserve dark and light themes and the existing grayscale UI language.

---

### Task 1: Acknowledged video handoff

**Files:**
- Modify: `src/boltsnapipc.cpp`
- Modify: `tests/test_boltsnapipc.cpp`
- Modify: `/home/mt/projects/boltsnap/src/shelf/mod.rs`

**Interfaces:**
- Consumes: existing framed `Response { ok, error }` format.
- Produces: `sendVideoToBoltsnapShelf` and `sendVideoToBoltsnapCard` return success only after Boltsnap accepts the request.

- [ ] Add an IPC test whose server returns `{"ok":false,"error":"full"}` and assert that video delivery fails with `full`.
- [ ] Run `cmake --build build --target test_boltsnapipc && QT_QPA_PLATFORM=offscreen ./build/test_boltsnapipc`; expect the new test to fail because Eddy closes without reading.
- [ ] Make Boltsnap video add/replace return `Result<(), String>` and write the existing framed response.
- [ ] Make Eddy video sends read and validate that response within the existing socket deadline.
- [ ] Re-run the targeted C++ test and `cargo test --no-default-features`; expect both to pass.

### Task 2: Pending video actions and cache invalidation

**Files:**
- Modify: `src/editorwindow.h`
- Modify: `src/editorwindow.cpp`
- Modify: `src/redactocrcontroller.h`
- Modify: `src/redactocrcontroller.cpp`
- Modify: `tests/test_editorwindow.cpp`
- Modify: `tests/test_redactocrcontroller.cpp`

**Interfaces:**
- Consumes: `finishVideoExportCache` completion and `RedactOcrController` signals.
- Produces: pending copy/save actions complete once; OCR result application emits `contentChanged`.

- [ ] Add a controller test asserting `contentChanged` after OCR result application.
- [ ] Add an editor test proving copy requested before export completion reaches the clipboard afterward.
- [ ] Run both targeted tests; expect failures for the missing signal and dropped pending action.
- [ ] Connect OCR `contentChanged` to `onVideoContentChanged` and store only the minimal pending delivery intent in `EditorWindow`.
- [ ] Re-run both targeted tests; expect passes.

### Task 3: Non-blocking save and timeline styling

**Files:**
- Modify: `src/editorwindow.cpp`
- Modify: `resources/eddy.qss`
- Modify: `tests/test_editorwindow.cpp`
- Modify: `tests/test_theme.cpp`

**Interfaces:**
- Consumes: current cached video path and background export completion.
- Produces: edited-video file saves reuse/copy the ready cache or wait asynchronously for it; trim timestamps share playback metadata styling.

- [ ] Add a test asserting a file-save intent is fulfilled from the completed cache without a second export.
- [ ] Add a QSS test covering `TrimInTime` and `TrimOutTime`.
- [ ] Run the targeted tests; expect failures.
- [ ] Route edited-video saves through the existing cache completion and copy the result atomically to the requested destination.
- [ ] Add both trim label selectors to the existing metadata rule.
- [ ] Re-run targeted tests, then the full Eddy suite.
- [ ] Render and inspect dark/light previews with `eddy_preview`.


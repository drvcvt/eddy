# Video Polish Completion Implementation Plan

> **For Codex:** Execute this plan task-by-task with tests before production changes. Preserve the existing `/tmp`-based fast path and keep temporary video ownership explicit.

**Goal:** Finish Eddy's video editing polish with reliable trim undo, readable controls, lightweight timeline previews, direct shelf delivery, responsive layout, and clear export feedback.

**Architecture:** Keep `EditorWindow` as the coordinator, add only small state APIs to `VideoTimeline`, and extend the existing ffmpeg/media and Boltsnap IPC paths. Timeline previews stay in memory; edited video files stay in the bounded temporary pipeline and are handed to Boltsnap with explicit ownership.

**Tech Stack:** Qt 6 Widgets/Multimedia/Test, C++20, ffmpeg/ffprobe, Rust/serde/unix sockets for Boltsnap.

---

## Task 1: Reliable trim interaction and readable playback controls

**Files:**
- Modify: `tests/test_editorwindow.cpp`
- Modify: `src/undocommands.h`
- Modify: `src/undocommands.cpp`
- Modify: `src/editorwindow.h`
- Modify: `src/editorwindow.cpp`
- Modify: `resources/resources.qrc`
- Create: `resources/icons/play.svg`
- Create: `resources/icons/pause.svg`
- Create: `resources/icons/volume.svg`
- Create: `resources/icons/muted.svg`
- Create: `resources/icons/reset.svg`

- [x] Add failing tests for trim undo/redo and precise In/Out labels.
- [x] Add a minimal undo command and separate state application from command creation.
- [x] Replace ambiguous playback text buttons with icons, tooltips, and accessible names.
- [x] Run the focused editor-window tests.

## Task 2: In-memory timeline contact sheet

**Files:**
- Modify: `tests/test_mediaio.cpp`
- Modify: `tests/test_videotimeline.cpp`
- Modify: `src/mediaio.h`
- Modify: `src/mediaio.cpp`
- Modify: `src/videotimeline.h`
- Modify: `src/videotimeline.cpp`
- Modify: `src/editorwindow.h`
- Modify: `src/editorwindow.cpp`

- [x] Add failing tests for ffmpeg contact-sheet generation and timeline image state.
- [x] Generate a small PNG contact sheet through stdout only; do not write preview files.
- [x] Load it asynchronously and render trim selection/dimming over it.
- [x] Run media and timeline tests.

## Task 3: Direct video delivery to Boltsnap shelf

**Files:**
- Modify: `tests/test_boltsnapipc.cpp`
- Modify: `src/boltsnapipc.h`
- Modify: `src/boltsnapipc.cpp`
- Modify: `src/editorwindow.h`
- Modify: `src/editorwindow.cpp`
- Modify: `/home/mt/projects/boltsnap/src/ipc.rs`
- Modify: `/home/mt/projects/boltsnap/src/shelf/mod.rs`

- [x] Add failing IPC round-trip tests for `add_video` and ownership flags.
- [x] Extend Eddy's client and Boltsnap's server dispatch.
- [x] For clean source clips, hard-link or copy into bounded temporary shelf storage; for exported clips, transfer temporary ownership.
- [x] Create the video card and thumbnail using Boltsnap's existing recording path.
- [x] Test both repositories' IPC paths.

## Task 4: Responsive toolbar and visible export state

**Files:**
- Modify: `tests/test_toolbar.cpp`
- Modify: `tests/test_editorwindow.cpp`
- Modify: `src/toolbar.h`
- Modify: `src/toolbar.cpp`
- Modify: `src/editorwindow.h`
- Modify: `src/editorwindow.cpp`

- [x] Add failing tests for compact toolbar visibility and export feedback.
- [x] Hide redundant shortcut-backed actions at narrow widths while retaining tools, color, shelf, and theme.
- [x] Show preparing, ready, saved/sent, and failure feedback only for user-requested exports.
- [x] Run focused UI tests.

## Task 5: Full verification

- [x] Build Eddy and run its complete test suite.
- [x] Run Boltsnap's relevant tests, then its complete test suite where practical.
- [x] Render and inspect dark/light plus narrow-window UI screenshots.
- [x] Review the final diff for temporary-file lifetime, regressions, and mt-ui-style consistency.

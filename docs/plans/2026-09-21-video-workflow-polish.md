# Video workflow polish: interaction and implementation plan

Date: 2026-09-21. Baseline: `e39f646` on `agent/current-eddy-release`.

The user wants the whole video workflow to feel more considered and has accepted
orientation improvements, precise trimming, interaction polish, loop/rate controls,
and copying an annotated frame. Space for play/pause is an explicit priority.
This plan was recorded before implementation. The execution record in section 6
reports the delivered behavior and actual verification; the design and validation
list below preserves the original intended coverage.
The baseline built successfully and passed all 24 CTest targets on 2026-09-20.

## 1. Product contract

Keep the compact grayscale interface, native Qt widgets, existing icon grid and
dark/light themes. Improve feedback and precision without adding a permanent
inspector panel. All four requested areas remain in scope.

| Area | Result |
| --- | --- |
| Orientation | Hover image and timestamp, adaptive filmstrip, selected duration |
| Precision | Editable In/Out, Shift fine adjustment, timeline zoom and navigation |
| Interaction | Space playback, stable seeking, clear drag/focus/loading states |
| Useful controls | Selection loop, preview speed, annotated frame copy |

### Playback and Space

- A Space tap toggles video playback once. Existing `K` continues to do the same.
- Preserve Space+left-drag for canvas panning and middle-button panning. Arm pan
  on key press; toggle playback on the matching key release only if the Space
  gesture was not used by a pointer interaction. There is no long-press timer.
- Consume auto-repeat without repeated toggles. A pointer gesture, Escape, focus
  loss, window deactivation or modal dialog cancels the pending Space toggle.
- While editing annotation text, Space inserts a space. Time inputs and open
  menus own their keys. Keyboard-focused buttons retain their native activation.
- Image documents retain their existing pan behavior. Tool selection and selected
  annotations do not change because of playback or navigation.
- Play outside `[In, Out)` starts at In. Pause leaves the displayed frame intact.
  Playback requested before media finishes loading is applied once when ready;
  a second toggle cancels that pending intent. Errors clear it.

### Scrubbing and trimming

- Hover never seeks the main player. Clicking or dragging the filmstrip does.
- On scrub start, remember playback intent and pause decoding playback. The
  pointer owns the target playhead until the gesture and final seek finish.
- Update the target and time display immediately. Coalesce player seek requests
  to a starting budget of one per 33 ms, keeping only the latest pending target.
  Send the final mouse-release position immediately, even without a preceding
  mouse-move event. This is throttling with a final flush, not an idle debounce.
- Resume after scrubbing only if playback was active before the gesture. If the
  chosen position is outside the selection, stay paused at that position; the
  next explicit Play starts at In. Do not jump away from what was just inspected.
- Trimming pauses and remains paused on release so the boundary can be checked.
  Show In, Out and selected duration live. A completed drag is one undo command;
  Escape restores its original range and position without creating a command.
- Shift reduces trim movement to one tenth. Anchor movement to the grabbed
  handle, preserving its pointer offset. Rebase the anchor when Shift changes
  during a drag, so pressing or releasing it never jumps the boundary.
- Keep one valid minimum interval and the existing source bounds. A click on a
  grip without moving it must not change the range or create undo/export work.
- Out is exclusive. When inspecting Out, preview the last included frame or
  nearest supported instant before it, while keeping the trim marker at Out.
- In/Out become unobtrusive editable time fields. Accept `ss[.mmm]`,
  `m:ss[.mmm]` and `h:mm:ss[.mmm]`, normalize display, reject invalid or crossed
  ranges visibly. Enter commits once; Escape restores; valid focus-out commits.
  Invalid focus-out restores the last valid value with brief feedback. Neither
  Enter nor Escape escapes to the window's Save/Close handlers while editing.
- `I`/`O` and Reset use the same validation and undo path as fields and handles.
  The selected-duration readout always reflects the draft during an interaction.

### Timeline view and orientation

- Default to the whole clip. Add a compact time ruler and adaptive thumbnail
  slots without enlarging the filmstrip into a second editing panel.
- Ctrl+wheel over the timeline zooms around the pointer; horizontal wheel or
  Shift+wheel pans the visible time range. Plain vertical wheel over the timeline
  does not leak into canvas zoom. Support high-resolution trackpad deltas.
- Provide focusable `Zoom in`, `Zoom out` and `Fit clip` actions as discoverable
  alternatives. Their shortcuts are scoped to timeline focus, leaving canvas
  `+`, `-`, `0` and `1` unchanged elsewhere.
- Store visible time range separately from trim range. Zoom/pan never edits the
  video, dirties export, or enters undo history. Keep global source timestamps.
- At a zoomed view edge, dragging can auto-pan with a bounded rate. Do not paint
  offscreen trim handles as if they were at the viewport edges. Show a small
  directional cue; returning to Fit always reveals the complete range.
- During playback, reveal a playhead leaving the viewport with a discrete view
  shift. Suspend follow during manual pan, hover inspection and active drags.
- Hover shows a small image above the strip after about 150 ms, with the target
  time. A cached coarse sample may appear first with its sample time; replace it
  with the requested preview when ready. Never present a stale image as exact.
- Use a child overlay constrained to the editor, transparent to pointer events,
  so the preview works on Wayland and stays inside narrow windows. Hide it on
  leave, Escape, focus loss and context menus. During trim, anchor it to the grip.
- Previews show source frames; the main canvas remains the annotated preview.
  Preview failure retains time-only feedback and never interrupts editing.

### Loop, speed and frame copy

- Loop repeats the committed In/Out selection, or the full clip when untrimmed.
  Default off. It does not alter export, undo, or the selection. Handle both a
  trimmed Out boundary and the backend's end-of-media event without double seeks.
- Offer `0.25×`, `0.5×`, `1×`, `1.5×`, `2×` in a compact speed menu, default `1×`.
  These affect preview only. Preserve rate across pause, seek and loop. Display
  the backend's accepted rate and fall back cleanly if it cannot apply a choice.
- Keep `J`/`L` as backward/forward small frame steps, pausing first. Use absolute
  frame-index calculations for nominal constant rates, avoiding cumulative
  rounding drift at 29.97/59.94 fps. Do not repurpose arrow keys used by annotations.
- `Copy frame` is a separate video action with `Ctrl+Shift+C`. Keep normal Copy
  and `Ctrl+C` as video delivery. Surface the frame action through the existing
  Copy control's menu; make the menu keyboard reachable and label both actions.
- Copy the currently presented frame plus every visible annotation at native
  document resolution, including Blur, OCR redaction and Spotlight. Exclude UI,
  selection handles and a text caret. Preserve selection, focus, undo and playback.
- If a seek is pending, copy only after its final frame is available. If the
  frame is unavailable or decoding fails, keep the existing clipboard and show
  one concise error. Never substitute a black placeholder or an older frame.

### Layout and feedback

- Wide layout: filmstrip and ruler above one row containing playback/current
  time, In/Out/duration, then loop/rate/audio. Frame copying remains an output action.
- At narrow widths, move the trim fields to a second compact row and put volume
  adjustment in its speaker popover. Keep Play, In/Out, Loop and speed reachable.
  Calculate the breakpoint from minimum widget sizes rather than truncating times.
- Match existing 22 px bar / 24 px floating-control sizes and icon treatment.
  Invisible grip hit areas may be larger than painted grips; show keyboard focus.
- Animate only presentation: roughly 80–120 ms hover emphasis and preview fade.
  Drag geometry, typed values, trim markers and seek targets respond immediately.
  Honor `animations=false` / `--no-anim` everywhere.
- During normal playback a short visual interpolation may smooth the playhead,
  but it is never the authoritative seek, trim, copy or export timestamp. Clamp
  it at Out, freeze on stall/pause, and reset on seeks and rate changes. Enable
  only after comparing real playback with timestamp-driven updates.
- Export feedback uses the existing background cache states: preparing, ready,
  failure. Reserve space so controls do not move. Do not invent percentage
  progress, restart a fade for every frame, or toast for every background update.

## 2. Current code and implementation boundaries

| Existing code | Relevant fact and intended change |
| --- | --- |
| `src/canvas.cpp` key and mouse handlers | Forwards keys to window; owns Space/middle pan. Report whether a Space gesture was consumed and clear it on cancellation. |
| `src/editorwindow.cpp` `createPlaybackBar`, `ensureVideoPlayer`, key handlers | Owns all playback wiring. Centralize toggle/seek/boundary logic into small methods here before adding controls. |
| `src/videotimeline.{h,cpp}` | Fixed 38 px strip, eight sampled frames, direct x-to-full-duration mapping. Add interaction lifecycle, draft/view range, navigation and hover intent. |
| `src/mediaio.{h,cpp}` | Contact sheet uses blocking ffmpeg in a detached worker and scans the requested duration. Reuse media metadata/argument helpers, introduce bounded asynchronous sampling for the interactive UI. |
| `src/editorwindow.cpp` `scheduleVideoExportCache` | Export starts after 350 ms. Suppress its timer during draft trim, commit only once, then resume existing scheduling. |
| `src/editorwindow.cpp` `renderAnnotationOverlay` | Intentionally hides video and blur for ffmpeg export. It cannot serve as annotated frame copy unchanged. |
| `src/exporter.{h,cpp}`, `items/redactitem.*` | Existing scene rasterization and blur caches can render the frame without implementing another annotation renderer. |
| `src/undocommands.*` | Reuse `SetTrimRangeCommand`. Navigation, loop, speed and preview are not document edits. |
| `resources/eddy.qss`, `src/theme.*`, `resources/eddy.qrc` | Extend current tokens and icon system; avoid per-widget parallel styling. |

Keep `EditorWindow` as coordinator. Do not extract a generic player/controller
framework. The justified new component is a small `VideoPreviewProvider` owning
one asynchronous `QProcess`, bounded image cache, and pending preview work. A
small time-field widget is justified only if validation/commit handling cannot
stay clear with `QLineEdit` and a validator. Register new compiled files in CMake.

### Authoritative time and interaction state

- Separate requested seek target, player-reported position and presented-frame
  timestamp. Backend position updates must not overwrite a pointer-owned target.
- Timeline emits interaction start, preview, finish and cancel; finish carries
  the final pointer position. Capture pre-gesture state once. Signals should
  describe intent; timeline painting must not call the player or exporter.
- Keep one latest pending seek, a gesture generation and a final-seek flag in
  `EditorWindow`. Ignore obsolete callbacks where identifiable. There is no seek
  request ID in `QMediaPlayer`, so a synthetic generation alone is insufficient:
  reconcile against presented-frame timestamps and the requested target.
- Qt exposes player position in milliseconds and frame timestamps in microseconds.
  Normalize units explicitly and handle unknown timestamps. `setPosition()` is
  not evidence that a matching frame has been decoded. See the official
  [player position documentation](https://doc.qt.io/qt-6/qmediaplayer.html#position-prop)
  and [frame timing documentation](https://doc.qt.io/qt-6/qvideoframe.html#startTime).
- Keep committed trim/export values in the existing millisecond representation.
  Frame stepping uses nominal fps only where appropriate. VFR remains time-based;
  do not claim universal frame accuracy or build a whole-file frame index in this
  increment. Validate CFR stepping and actual decoded output separately.
- Missing timestamps use a conservative settled-frame fallback; copying during
  an unresolved seek fails rather than copying a potentially unrelated frame.
  A bounded timeout/error path must always release interaction ownership.
- Playback and ffprobe can report slightly different durations. Keep source
  bounds and edited range consistent, refresh both fields, and distinguish
  metadata reconciliation from a user undo command.

### Bounded preview work

- Native `QProcess` signals perform start/read/finish/error handling. No
  `waitForFinished()` on the GUI thread and no new thread/process per mouse event.
- One preview process at a time, one latest hover request and a bounded list of
  currently visible thumbnail times. Hover/trim inspection takes priority over
  filling the strip. Invalidate outdated view generations on zoom/resize.
- Start with 6–24 thumbnail slots based on logical strip width and aspect ratio,
  bucketed to avoid rebuilds for every resize pixel. Sample only visible times
  with targeted seeks, rather than decoding an entire long clip for one strip.
- Use accurate input seeking and one downscaled frame to stdout per sample.
  FFmpeg may decode from an earlier seek point; this is why process count and
  request replacement are bounded. See [FFmpeg seeking semantics](https://ffmpeg.org/ffmpeg.html#Main-options).
- Cache images by source identity, requested time and pixel-size bucket, with a
  starting total budget of 32 MiB. Include device-pixel ratio in size selection.
  Drop least-recently-used entries; never persist preview files.
- Throttle replacement requests while the pointer moves and use a short idle
  delay before exact hover extraction. Cancel obsolete work on leave/close and
  view changes without repeatedly killing useful work for tiny pointer movement.
- Bound stdout/stderr, frame dimensions, queued work and process runtime. On
  malformed output or timeout, retain already usable samples and time feedback.
  Window destruction disconnects callbacks and terminates only its own preview
  process. Preview failure cannot affect the export process or source file.

### Annotated frame rendering

- Keep a presented-frame image/timestamp pair from the video sink, not a fresh
  screenshot of the canvas. Freeze that pair for the duration of a copy operation.
- Render synchronously on the GUI thread with the video surface temporarily
  replaced by the frozen image at background z-order. Keep blur items visible and
  sourced from that same frame. Use the existing scene renderer at native size.
- Restore background, selection, handles, focus and temporary visibility on all
  paths using a scoped guard. Do not trigger content revision or export work.
- Test rotation, mirroring and pixel aspect ratio before sharing the existing
  `m_bg` conversion. `QVideoFrame::toImage()` does not itself apply presentation
  transforms. See [Qt image conversion](https://doc.qt.io/qt-6/qvideoframe.html#toImage).
- Protect unresolved OCR regions as the existing renderer does. Rendering a
  snapshot must never accidentally drop blur because video export hides it.

## 3. Sequential implementation slices

Each slice is independently reviewable. Add regression tests for the behavior
being changed, implement it, run its focused targets, then move to the next slice.
Use the existing branch state as the base; keep any new commits free of agent
co-author trailers. No subagents are part of this plan.

### A. Space playback and shared transport actions

Files: `src/editorwindow.{h,cpp}`, `src/canvas.{h,cpp}`,
`tests/test_editorwindow.cpp`, `tests/test_canvas.cpp`, `README.md`.

- Introduce a shared playback-toggle method used by button, K and Space.
- Implement tap-versus-pan arbitration and scoped focus/input handling.
- Cover held/repeated Space, release after pan, Escape, focus loss, text entry,
  image mode, focused controls, and toggling while the source is still loading.
- Verify normal playback/pause with a real generated clip, not just a button spy.
- Restore middle-button and Space-drag camera panning even at Fit or when the
  image is smaller than the viewport. Extend only the view's navigation bounds;
  keep the document scene bounds, fit target and export dimensions unchanged.
- Replace oversized native tooltip surfaces with compact editor-owned hints:
  3 px vertical / 6 px horizontal padding, rounded 7 px corners, existing fonts
  and theme tokens. Keep hints pointer-transparent and within the editor on
  Wayland. Verify both themes visually and test dismissal/focus behavior.

These last two items were explicitly added by the user with a tooltip screenshot.

### B. Scrub lifecycle, fine trim and editable times

Files: `src/videotimeline.*`, `src/editorwindow.*`, `src/undocommands.*` only if
necessary, `tests/test_videotimeline.cpp`, `tests/test_editorwindow.cpp`.

- Add begin/finish/cancel state, handle grab offsets, Shift fine movement,
  final release coordinates and the coalesced player-seek path.
- Separate draft from committed trim and protect drag ownership from backend
  feedback. Suspend pending background export during a draft; keep valid cache.
- Add In/Out editors and selected duration using the existing undo command.
- Test Shift transitions, grip no-op clicks, handle crossing, cancellation,
  stale feedback, final seek, playback resume policy and one undo per gesture.
- Test invalid/pasted/long time values, Return/Escape/focus-out, duration
  reconciliation, and no export work for draft changes or navigation.

### C. Timeline zoom, ruler and view navigation

Files: `src/videotimeline.*`, `src/editorwindow.cpp`,
`tests/test_videotimeline.cpp`, `tests/test_editorwindow.cpp`.

- Introduce visible range and central coordinate conversion for every hit
  test, paint operation and pointer-time calculation.
- Add anchored zoom, wheel/trackpad pan, Fit, scoped keyboard actions, edge
  auto-pan and offscreen trim cues. Recompute ruler ticks from visible scale.
- Test zoom anchoring, short/long/zero durations, view clamping, offscreen
  handles, range-independent undo, resize, and canvas/timeline shortcut isolation.

### D. Adaptive thumbnails and hover/trim previews

Files: new `src/videopreviewprovider.{h,cpp}`, `src/mediaio.*`,
`src/videotimeline.*`, `src/editorwindow.*`, `CMakeLists.txt`,
new `tests/test_videopreviewprovider.cpp`, `tests/test_mediaio.cpp`.

- Implement the single-process scheduler, bounded cache and generation checks.
- Wire visible sample requests and a Wayland-safe preview child overlay.
- Retire the UI's old one-off eight-frame worker once parity is covered;
  keep or simplify the media helper according to remaining callers.
- With synthetic numbered/color frames, verify time-to-sample mapping,
  changing zoom, DPR and resize. Test latest-request priority, timeout, malformed
  output, cache eviction and closing with a request in flight using a fake process.
- Check long-GOP playback and preview contention on a real local clip before
  tuning the initial 33 ms / 150 ms / 32 MiB budgets.

### E. Loop, preview rate and improved frame steps

Files: `src/editorwindow.*`, `src/mediaio.*` only for necessary time helpers,
`resources/icons/`, `resources/eddy.qrc`, `tests/test_editorwindow.cpp`.

- Add loop and speed controls, and centralize Out/end-of-media handling.
- Pause before J/L and compute steps from the absolute nominal frame index.
- Test loop on/off at trimmed and full ends, very short ranges, range changes
  while looping, seeking outside selection and preservation of rate after resume.
- Check rate choices against actual backend state. Guard any optional
  pitch-compensation API for Qt versions: CI includes Qt 6.8.3, while those APIs
  start at 6.10. See [Qt playback properties](https://doc.qt.io/qt-6/qmediaplayer.html#pitchCompensation-prop).
- Assert loop/rate changes do not dirty export or alter audio in saved output.

### F. Copy the annotated current frame

Files: `src/editorwindow.*`, `src/toolbar.*`, `src/exporter.*` if needed,
`tests/test_editorwindow.cpp`, `tests/test_toolbar.cpp`.

- Add the separate output action and shortcut, including keyboard menu access.
- Implement frozen-frame scene rendering and safe waiting for a final seek.
- Pixel-test native-size output with text, Blur, OCR redaction and Spotlight;
  assert no selection decorations, placeholder frame or stale-seek image.
- Verify rotated input and restoration of selection/focus/playback. Keep
  clipboard contents on failure and normal video Copy/Drag-out/Shelf unchanged.

### G. Visual feedback and responsive layout

Files: `resources/eddy.qss`, `src/theme.*`, `src/videotimeline.*`,
`src/editorwindow.*`, `tools/eddy_preview.cpp`, relevant widget/theme tests.

- Finish wide/narrow transport layout, stable time widths and focus states.
- Add hover/preview transitions respecting the animation setting; measure
  whether timestamp updates alone suffice before adding playhead interpolation.
- Integrate quiet preparing/ready/error feedback using existing export state.
- Extend preview scenarios for hover, zoomed/trimmed views, focused time
  fields, loading, loop/speed and narrow layout in both themes.
- Inspect actual renders at 100% and high DPI. Check control reachability and
  text clipping; screenshot tests alone do not establish playback smoothness.

### H. Integrated acceptance and documentation

- Update README controls, shortcuts and the preview-only meaning of speed.
- Build and run the complete existing and new test suite once all slices pass.
- Exercise real playback, repeated scrub/trim/undo, input focus, loop, rate,
  hover while playing, frame copy after seek, and Copy/Save/Drag-out/Shelf delivery.
- Check a short CFR clip, 29.97/59.94 fps, VFR screen recording, long-GOP clip,
  4K input, portrait rotation, audio/no-audio, and a long clip at narrow width.
- Verify requested output still corresponds to the final committed range and
  annotations, with synchronized audio and no source mutation on failures.

Focused commands use the current Release tree, for example:

```sh
cmake --build build-rel --parallel 3 --target test_canvas test_videotimeline test_editorwindow
ctest --test-dir build-rel -R '^(test_canvas|test_videotimeline|test_editorwindow)$' --output-on-failure
```

After adding a target, regenerate CMake. Final verification:

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --parallel 3
ctest --test-dir build-rel --parallel 3 --output-on-failure
```

## 4. Completion criteria and limits

All four accepted feature groups, including Space playback, must be present.
Automated checks establish correctness; real interaction checks establish the
feel. Record clip/backend details for performance observations instead of
claiming smoothness solely from passing Qt tests.

No new dependencies, general editing framework, multitrack editing, per-annotation
timing, persistent project format, export speed changes or blanket encoder
rewrite are required. Accurate VFR frame indexing is a separate media feature;
this plan keeps temporal precision honest rather than promising it from fps alone.
Performance changes to per-frame redaction/conversion require measurements and
must preserve the current protection of sensitive content.

Implementation starts with A, then B. Those establish keyboard and interaction
semantics used by the subsequent visual and feature work.

## 5. Git audit and authorized completion

The user explicitly requested implementation of every accepted item and a clean
final `main`, with no remaining work branches holding changes outside `main`.
Do not merge unrelated or unapproved work as part of cleanup.

Read-only audit on 2026-09-21, verified against GitHub via authenticated HTTPS:

- Local and remote `main` are `07267d2`, the merge of PR #6 on 2026-09-20.
- Local and remote `agent/current-eddy-release` are `e39f646`. Its file tree is
  identical to `main`; GitHub reports zero commits ahead, one merge commit behind.
- Yesterday's 11 feature/fix commits cover arrows, desktop integration, filmstrip,
  Vis styling, bar sizes and icon/text glyph polish. All are already in `main`.
- GitHub has exactly those two branches and no open pull requests. Local
  `refs/t3/checkpoints/*` are tool checkpoints, not unmerged work branches.
- The only new working-tree change at audit time is this plan.
- `main` requires the `linux` and `windows` CI checks and an up-to-date base;
  no approving review is required. Preserve this protection and use a PR.

Completion sequence:

1. Create one scoped video-polish branch from the freshly fetched `main`.
2. Implement and test the accepted plan, review the complete diff against main,
   and commit only its code, tests, assets and documentation.
3. Push using the existing GitHub HTTPS credentials without altering global
   authentication settings. Open the PR, pass both required checks, fix any
   failures and merge normally. No force push or protection bypass.
4. Verify remote main contains the tested work. Update local main with a
   fast-forward, then delete the fully merged feature and old release branches
   locally and remotely after verifying ancestry. Do not delete tool checkpoints.
5. Report final main commit, CI/test result, clean worktree and branch/PR inventory.
   Any newly appeared unrelated branch is audited separately, never auto-merged.


## 6. Execution record (2026-09-21)

All four accepted feature groups and both screenshot follow-ups are implemented.
Work was committed in the planned order A through F, followed by responsive
layout, feedback, integrated fixes and documentation. No dependencies were added.
The Linux CI setup now installs GStreamer's existing MP4/H.264 runtime plugins:
Ubuntu's Qt 6.4 uses that backend, and the new real-playback tests exposed missing
decoders in the runner. This adds no application library or feature dependency.

| Slice | Delivered |
| --- | --- |
| A | Shared Space/K/button toggle, repeat/pan/focus arbitration, camera navigation bounds at Fit, compact rounded child tooltips |
| B | Gesture begin/finish/cancel, coalesced seeks with final flush and decoded-frame acknowledgement, Shift fine trim, editable validated times, one undo per trim |
| C | Independent timeline viewport, anchored zoom, wheel/keyboard/context-menu navigation, ruler, edge pan, offscreen grip cues |
| D | One asynchronous decoder, latest hover priority, bounded 32 MiB image cache, adaptive filmstrip, coarse-to-exact child preview |
| E | Selection/full-clip loop, preview-only rates, absolute nominal frame steps without accumulated 29.97 fps rounding |
| F | Copy-frame menu and shortcut, final-seek waiting, frozen annotated rendering with blur/OCR/Spotlight and restored text focus |
| G | Wide/narrow transport, speaker volume menu, stable time widths, optional 100 ms preview fade, quiet export status |
| H | README updated, complete local build and 25/25 CTest targets passed; required Linux/Windows CI gates the normal PR merge |

Implementation decisions:

- Real decoded-frame timestamps drive the playhead, replacing coarse position
  feedback while playing. No synthetic interpolation was added. Grip emphasis
  changes immediately; only preview appearance fades when animations are enabled.
- Latest-request keys and visible timestamp membership reject obsolete preview
  results; a separate generation counter is unnecessary for the immutable source.
- On this Qt FFmpeg backend, seeking exactly to a frame boundary returned the
  preceding frame. Seeking 1 ms inside the requested boundary fixed the observed
  failure; acknowledgement still checks the actual presented frame interval.
- The Qt 6.4 GStreamer run exposed an 80 ms buffer timestamp origin for the
  generated H.264 clip. Frame timestamps are anchored to the first presented
  frame at source position zero before reconciliation with player positions.
  No offset is inferred from a seek result. The real-media test compares source
  frame intervals relative to this initial timestamp and verifies copied pixels.
- Full-file playback may finish in Qt's Stopped state; trimmed playback pauses.
  Both retain the final included frame, and both end paths support looping.
- Frame copying applies presentation rotation/mirroring and keeps letterboxing
  inside the existing document canvas. It does not redefine encoded dimensions,
  the export coordinate system, or support for unusual pixel aspect ratios.
- The existing contact-sheet helper remains for its independent callers/tests;
  the editor no longer starts its detached, whole-clip contact-sheet worker.

Verification performed:

- Release configure/build and all 25 CTest targets passed on Linux with Qt 6.11.1
  and FFmpeg 8.1.2. Targeted checks additionally cover Space versus text/pan,
  invalid/cancelled time edits, final-release trim, offscreen grip hit tests,
  cache reuse/latest hover, loop/rate undo isolation, and layout at 520 px with
  ten-hour timestamps.
- A real red/blue H.264 clip tests pause/scrub/resume, queued frame copy after
  seeking across a GOP, 2x short-selection looping, stopping at exclusive Out,
  full-file looping and the final decoded frame. Redaction pixel tests include
  a checkerboard blur, OCR blackening, Spotlight and active text focus.
- Actual editor renders were inspected in dark/light, wide/narrow, and 2x DPI,
  including hover over a zoomed timeline. Compact tooltip renders were checked
  in both themes. Preview process and image bounds were reviewed in code.
- Generated playback smoke clips: 640x360 H.264/AAC at 30000/1001 fps with an
  eight-second GOP; 3840x2160 at 60000/1001 fps; 10-to-30 fps VFR; and a MOV/MP4
  display-matrix rotation of 90 degrees. The rotated frame copy was inspected.
  These are decode/render checks, not a measured latency or dropped-frame claim.
- Existing delivery/export tests cover final trim output, retained annotations,
  audio, independent saves, clipboard lifetime and asynchronous shelf handoff.

Validation limits: no human Windows/Wayland interaction session, subjective audio
pitch check, real screen-recording corpus, or exhaustive fake-process timeout/
cache-eviction fault matrix was performed. Failure paths retain time-only hover
feedback or the existing clipboard; a two-second seek timeout prevents indefinite
pending frame copy. VFR remains time-based, as specified in the product contract.

Git cleanup follows section 5, using only the audited approved work. The PR and
required CI results are the authoritative integration record.

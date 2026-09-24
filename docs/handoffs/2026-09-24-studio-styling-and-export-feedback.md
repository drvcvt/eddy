# Handoff: Studio styling, Boltsnap cursor contract, export feedback

Date: 2026-09-24
Project: `/home/mt/projects/eddy`

## Goal / current status

- User wants Eddy to gain an optional "Studio" part in the style of screen.studio, for images and videos. Studio must never be mandatory: off by default, no cost when unused.
- Completed in this session (all **uncommitted**, working tree on `main`, HEAD `2910125`, 4 commits ahead of the locally recorded `origin/main`; no push authorized):
  1. Video export feedback: live progress, stall detection, visible error reasons, a Cancel button.
  2. Research on screen.studio and a phased adoption plan.
  3. Cursor sidecar contract v1 with Boltsnap, plus a loader in Eddy (not yet used visibly).
  4. Studio styling: background (none, 6 presets, own image), padding, corners, shadow, output ratio, live canvas preview, image and video export, frame copy, undo, remembered last style, output size shown in the popover.
  5. A review pass fixed three bugs (odd video offset 1 px line, double popover, cursor time rounding at segment seams) and made Studio video export as fast as plain export via `maskedmerge`.
- User's request for the next session: **fully plan the remaining screen.studio features first, then implement only after everything is planned.** Design questions go to the user with rendered visuals.
- UI verification: offscreen renders only (`eddy_preview`, throwaway probes), dark and light inspected. Not clicked through in a real Eddy window by an agent. User said they like the Studio addition.
- `build-rel/eddy` rebuilt 2026-09-23 23:38; `~/.local/bin/eddy` points to it.

## Files changed

Export feedback (earlier in the session):
- `src/videoexporter.{h,cpp}`
  - `VideoExportRequest::progress` (0-99 or -1), `cancelled`, `stallTimeoutMs` (60 s). ffmpeg runs with `-nostats -progress pipe:1`; `out_time_us` drives progress; no output-time advance for `stallTimeoutMs` kills ffmpeg (hardware encoder then falls back to CPU). `FailedToStart` reported correctly.
  - Pre-existing uncommitted change kept: drop frames to 60 fps before filters for >60 fps sources (`probeVideoFile`); its source duration is reused for progress.
- `src/editorwindow.{h,cpp}`
  - Status label shows `Exporting N%`; failure toast shows ffmpeg's last stderr line (6 s), full error in the status tooltip.
  - `m_exportCancel` button (`VideoExportCancel`) beside `VideoExportStatus`, visible only during export; `cancelVideoExport()` kills ffmpeg, drops pending actions, lets a pending window close proceed.

Cursor sidecar (Boltsnap contract):
- `src/cursortrack.{h,cpp}` (new): `cursorTrackPathFor` (`clip.mp4` → `clip.cursor.json`), `parseCursorTrack`, `loadCursorTrack`, `CursorTrack::positionAt` (linear, no interpolation into hidden stretches), `cleanVideoPath` only for an existing sibling file. Fractional ms accepted; up to 1 ms step back clamped, larger regressions rejected. 128 MB limit.
- `src/mediaio.{h,cpp}`: `MediaDocument::cursorTrack` (optional), `LoadMediaResult::warning`. `src/main.cpp` prints the warning; a broken track never blocks opening.

Studio styling:
- `src/studiostyle.{h,cpp}` (new): `StudioStyle` (lengths in percent of the content's shorter side), `studioBackgroundPresets()`, `studioLayout()` (even output size, **even content origin**), `renderStudioBackground`, `renderStudioMask`, `renderStudioFrameMask` (ffmpeg coverage), `renderStudioImage`, `loadLastStudioStyle`/`saveLastStudioStyle` (`[studio]` group in the config INI; default Dusk). Background image decode cached (mutex, path+mtime key).
- `src/studiopopover.{h,cpp}` (new): `StudioPopover(style, contentSize)`, translucent popup with own `paintEvent`, swatches, sliders, ratio chips, `StudioSize` label.
- `src/toolbar.{h,cpp}`: `Studio` button (icon + label) before Save; `setStudioActive`, `studioRequested`.
- `src/canvas.{h,cpp}`: `setStudioFrame`/`clearStudioFrame`, `viewRect()` for Fit; `drawForeground` paints the background ring around rounded content (same picture as export).
- `src/editorwindow.{h,cpp}`: `m_studioStyle`, `setStudioStyle`, `openStudio` (toggle if open), `updateStudioPreview` (preview rendered at ≤1600 px), one undo step per popover session (`SetStudioStyleCommand` in `src/undocommands.h`), `exportComposite` and `copyVideoFrame` apply Studio, `hasVideoEdits` includes Studio, `request.studio`.
- `src/videoexporter.cpp`: Studio video path = `pad` to output size → `maskedmerge` with an opaque background still and a gray coverage mask (chroma planes via `mergeplanes` at half size, `setsar=1` on both stills).
- `resources/icons/studio.svg` (new, normalized to the icon grid), `resources/eddy.qrc`, `resources/eddy.qss` (popover, Studio button, size label).
- `tools/eddy_preview.cpp`: modes `studio`, `studio-open` (also combinable with `video-file`).
- `CMakeLists.txt`: new sources and tests.

Docs:
- `docs/plans/2026-09-23-screen-studio-research.md` (new): feature catalogue, technique, phase plan, sources.
- `docs/specs/2026-09-23-studio-mode.md` (new): Studio principles, UI decisions, cursor contract v1, what Boltsnap actually delivers, implementation notes and measurements.
- `README.md`: Studio paragraph.

Tests:
- `tests/test_cursortrack.cpp`, `tests/test_studiostyle.cpp` (new).
- `tests/test_videoexporter.cpp`: stall, cancel, progress, Studio framing (odd offset edges, fps, duration), Studio with annotations.
- `tests/test_editorwindow.cpp`: failure reason visible, cancel, Studio off by default, image export framing, one undo step + remembered style + output size label, Studio button visible.

## Files inspected

- `src/videoexporter.cpp`, `src/editorwindow.cpp` (export cache, pending actions, close handling), `src/canvas.cpp` (crop foreground), `src/toolbar.cpp`, `src/colorpopover.cpp`, `src/cropbar.cpp`, `resources/eddy.qss`, `src/theme.h`, `tools/normalize_icons.py`, `tests/test_theme.cpp` (icon grid test).
- `docs/plans/2026-09-21-crop-audio-projects-annotations.md` (open plan: Audio → resumable projects → annotation helpers; it had excluded time-ranged annotations and multi-track editing).
- Boltsnap: `docs/plans/2026-09-23-performance-functionality.md` (Phase 4 cursor), `src/record/cursor.rs::sidecar_json`, `src/platform/linux/gsr.rs`.

## Key decisions / assumptions

- Studio is optional: off for every new document; clicking Studio turns it on with the last used style; "None" turns it off. UI stays grayscale; color only in exported content.
- User-approved (rendered comparisons): button placement top bar before Save; presets mix (Graphite, Paper, Dusk, Ocean, Sunset, Mint); defaults padding 8 / corners 2 / shadow 50; "always off, last style preselected". Later picks: output size label yes, faster video export yes; custom colors and wallpaper blur **not** chosen for now.
- Cursor: three ways (user preference). Boltsnap bakes a smoothed cursor so files work without Eddy; Eddy keeps that by default; Eddy may re-render or drop the cursor using `clean_video` + track. Never deliver a cursorless video silently.
- Boltsnap facts (from Boltsnap session, `1ff3923`, now in Boltsnap `main`): sidecars only when `record_cursor` is `mellow`/`quick`; no `clicks` ever (Hyprland 0.56); one `arrow` image; springs mass 1 critically damped, mellow 170/26, quick 600/49; replay clips have no track. No real `*.cursor.json` existed on disk when checked.
- Boltsnap and Eddy stay decoupled: files only, no IPC or CLI handoff.
- Design rules: `mt-ui-style` skill plus Eddy's `resources/eddy.qss`/`src/theme.h`; `chromeFollowsTheDensityRules` test enforces icon ≤ text (Studio label uses `@fs-body` like "To shelf").
- Keep exports explicit (automatic background exports were rejected earlier for resource use). Build with `--parallel 2`. No subagents unless the user asks. No AI co-author trailers. Delete files only via `/home/mt/.local/bin/safe-rm` after inspecting targets. Never run or touch Hyprlock.
- ffmpeg lessons: `overlay` takes timing from its first input (looped stills as first input forced 25 fps); `pad` silently rounds odd offsets on yuv420; RGBA overlays over full frames are slow; Qt PNGs carry a DPI that ffmpeg reads as SAR 3780:3780.

## Commands run and results

- `cmake --build build-rel --parallel 2` then `cd build-rel && ctest`: last run after the output-size feature: **28/28 test executables passed** (~34 s). `git diff --check` clean.
- Real-clip measurement (throwaway probe `/tmp/studio-probe/export.cpp`, source `~/Bilder/boltsnap/boltsnap-2026-09-21_18-25-58.mp4`, 1894×1026 @240 fps, 5 s trimmed, 60 fps out): plain 2.73 s, Studio 2.54 s after `maskedmerge` (was 4.8 s vs 2.0 s with full RGBA overlay). Background colors within 2/255 after YUV. Output 2058×1190, 300 frames.
- Filter-only timing (`/tmp/studio-bench.sh`): base 1.30 s, full overlay 3.02 s, maskedmerge 1.57 s.
- Canvas repaint benchmark (`/tmp/studio-probe/bench.cpp`): Studio adds ~0.4 ms per full viewport repaint, nothing measurable for media-rect repaints.
- One earlier full run had `test_crop` `videoCoordinatesAgree(landscape)` hit Qt's 300 s watchdog; 6 immediate reruns passed. Not reproduced; cause unknown (suspected offscreen media backend).

## Open blockers / risks

- Everything is uncommitted; the user has not asked for commits. Do not commit or push unless asked.
- Not verified in a real, clicked Eddy window (Wayland popup positioning, rounded translucent popover, file dialog for "Image…").
- Light theme: the Paper swatch is barely visible on the light popover.
- Cursor track is loaded but unused; no real Boltsnap sidecar has been parsed yet.
- The intermittent `test_crop` watchdog hang above.
- Boltsnap working tree has an uncommitted change in `src/platform/linux/shelf/mod.rs` from another session; do not touch Boltsnap.

## Exact next steps

The user wants: **plan all remaining features completely first, implement only after the plan is approved.**

1. Re-check `git status` in Eddy and Boltsnap; read the specs/plans listed below.
2. Write one planning document (e.g. `docs/plans/2026-09-24-studio-features.md`) covering every remaining feature from the research catalogue, with dependencies and order:
   - Composition model + frame-render export path (decode pipe → Qt render → encode pipe), only active when time-varying Studio features exist; keep the current ffmpeg path otherwise.
   - Zoom segments (timeline lane, context bar, target in canvas, spring camera "Focused"/"Smooth", instant), preview via scene transform, motion blur (optional).
   - Cursor-track features: follow cursor, auto-zoom suggestions by dwell (no clicks available), Eddy cursor re-render/none via `clean_video` (Boltsnap springs as defaults).
   - "Always keep zoomed in" for 9:16 etc.
   - Fragments: split, multiple cuts, speed per fragment (ffmpeg trim/setpts/atempo/concat).
   - Time-ranged masks/highlights (conflicts with the 2026-09-21 plan's exclusion: user must decide).
   - Export: GIF, export presets; shareable Studio presets.
   - Audio items and captions only if the user keeps them in scope; reconcile with the open 2026-09-21 Audio plan.
   - Explicitly out: webcam, iOS, cloud share links, click effects/sounds, cursor shapes, shortcut overlay, typing speed-up (no data from Hyprland 0.56).
3. For every design question, render real Qt visuals (`eddy_preview` or a throwaway probe in `/tmp`) and ask the user before fixing it in the plan.
4. Get plan approval, then implement phase by phase with tests per phase (generated ffmpeg clips, pixel probes, preview vs export equality, spring model unit tests).

## Useful resume commands

```sh
cd /home/mt/projects/eddy
git status --short --branch
git diff --stat
git -C /home/mt/projects/boltsnap status --short --branch
cmake --build build-rel --parallel 2
(cd build-rel && ctest)
# /tmp is not durable: recreate the sample frame first
ffmpeg -v error -y -ss 5 -i ~/Bilder/boltsnap/boltsnap-2026-09-21_18-25-58.mp4 -frames:v 1 -vf scale=1280:-2 /tmp/studio-frame.png
QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-studio.png dark studio-open /tmp/studio-frame.png
QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-studio-video.png dark video-file-studio ~/Bilder/boltsnap/boltsnap-2026-09-21_18-25-58.mp4
```

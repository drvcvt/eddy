# Handoff: Studio plan, model, render export, preview quality

Date: 2026-09-24
Project: `/home/mt/projects/eddy`

## Goal / current status

- The user wanted every remaining Screen-Studio feature for Eddy's optional "Studio" part planned completely,
  with every design question asked through real Qt renders, then implemented phase by phase with tests.
- Done in this session:
  1. Full plan `docs/plans/2026-09-24-studio-features.md`. All design questions (Q1 to Q7 plus follow-ups) and
     scope questions (N1 to N5) answered by the user. Only N6 (spring values) is still open.
  2. Phase S0: Studio document, JSON format, time map, camera springs (no UI). Plan
     `docs/plans/2026-09-24-studio-s0-model.md`.
  3. Phase S1: frame-rendered video export for zoom segments. Plan
     `docs/plans/2026-09-24-studio-s1-render-export.md`.
  4. Scope change by the user: the cursor is decided and baked in by Boltsnap. Eddy never re-renders it and
     offers no cursor choice. Removed from the model, plans and spec.
  5. Video/image preview quality at any zoom. The fix was done by a subagent at the user's request, then
     made cheaper for high-fps playback by me.
  6. Studio and "To shelf" icons centred on the label capitals, 8 px gap.
  7. The 2026-09-23 session's uncommitted work was committed as well: export progress/stall/cancel,
     cursor track loader, Studio styling, docs.
- Everything is committed on branch `feat/studio`, branched from local `main` at `2910125`. That base is
  itself 4 commits ahead of the locally known `origin/main`. **Not pushed.** The user reviews first and then
  approves the push. Ask whether to push the branch (and open a PR) or to merge into `main`.
- UI verification: offscreen renders only, dark and light. Nothing was clicked in a real Eddy window.

## Files changed

Earlier session's work (2026-09-23), committed today:
- `src/cursortrack.{h,cpp}`, `src/mediaio.{h,cpp}`, `src/main.cpp`: cursor sidecar loader, warning on stderr.
- `src/studiostyle.{h,cpp}`, `src/studiopopover.{h,cpp}`, `src/toolbar.{h,cpp}` (Studio button),
  `src/canvas.{h,cpp}` (Studio frame preview), `src/editorwindow.{h,cpp}`, `src/undocommands.h`,
  `resources/eddy.qss`, `resources/eddy.qrc`, `resources/icons/studio.svg`, `tools/eddy_preview.cpp`:
  Studio styling for images and videos.
- `src/videoexporter.{h,cpp}`: progress, stall timeout, cancel, Studio via `pad` plus `maskedmerge`.
- Tests: `test_cursortrack`, `test_studiostyle`, and additions in `test_editorwindow`, `test_videoexporter`.

S0, model (no UI yet):
- `src/studiodocument.{h,cpp}`
  - `ZoomSegment`, `Fragment`, `StudioDocument`; `timeVarying()` is true only for zooms.
  - `studioToJson`/`studioFromJson`: version 1, a strict whole-block reject with a reason, limits of
    1000 items. Source ms and document pixels. No cursor fields (Boltsnap decides the cursor).
- `src/timemap.{h,cpp}`: trim plus fragments, source ↔ output time. A seam belongs to the later piece.
- `src/camerapath.{h,cpp}`
  - Critically damped spring, mass 1, exact per 1/240 s step, cached samples, Hermite evaluation.
  - Focused ω 10 (100/20), Smooth ω 6 (36/12), Instant jumps. Zooms less than 1 s apart hand over directly.
  - The camera is clamped into the content. `CameraFrame::aspect` gives a narrower base for "keep zoomed in".

S1, render export:
- `src/studiorenderer.{h,cpp}`: one output frame. Camera window of the source, annotations at any overlay
  resolution, then the Studio frame (background with the coverage mask as alpha) on top.
- `src/renderexport.{h,cpp}`: `RenderPipeline`/`runRenderPipeline`.
  - Decoder QProcess thread, drawing on the caller's thread, encoder QProcess thread.
  - Queues bounded to 128 MB each. Cancel, timeout, stall and progress.
- `src/videoexporter.{h,cpp}`
  - `VideoExportRequest::zooms`: non-empty uses the renderer. `writeVideoRendered()` always renders.
  - Shared helpers `appendBlurFilters`, `encoderCodecArgs`, `finishOutput`; the filter-graph path is unchanged.
  - Renderer output is always 60 fps, converted and tagged BT.709. Audio comes straight from the source.

Preview quality:
- `src/previewitems.{h,cpp}` (new): `DownscaledImage`, `PreviewPixmapItem`, `PreviewVideoItem`.
  - Below 100 %: area filter at the exact device size. Exact 2×2 halvings first, Qt's area filter for the rest.
  - 100 to 200 %: bilinear. From 200 %: crisp pixels.
  - Video frames are converted once each, and not more often than the screen refreshes (judged in media time).
    A skipped frame schedules its own repaint.
- `src/editorwindow.cpp`: the image background, the video item and the still use these items (4 lines).

Icons:
- `src/toolbar.cpp`: `LabelButton` paints the icon itself, measuring the glyph ink.
  - The ink sits on the cap middle, snapped to device pixels, 8 px from the label and 8 px from the edge.
  - Used for Studio and To shelf.
- `resources/eddy.qss`: removed the now unused padding rule for those two buttons.

Tests added: `test_studiodocument`, `test_timemap`, `test_camerapath`, `test_studiorenderer`,
`test_renderexport`, `test_previewitems`. Also `test_videoexporter` (renderer vs filter graph, zoom target,
trim and audio), `test_editorwindow::shrunkVideoMatchesAnAreaFilteredFrame` and
`test_toolbar::labelledButtonsCentreTheirIconOnTheCapitals`.

Docs:
- `docs/plans/2026-09-24-studio-features.md` (the plan and all decisions), the S0 and S1 implementation plans.
- `docs/specs/2026-09-23-studio-mode.md` (status, cursor decision, Boltsnap facts).
- `docs/plans/2026-09-21-crop-audio-projects-annotations.md` (exception note), `README.md`.

## Files inspected

- Docs: `docs/handoffs/2026-09-24-studio-styling-and-export-feedback.md`, `docs/specs/2026-09-23-studio-mode.md`,
  `docs/plans/2026-09-23-screen-studio-research.md`, `docs/plans/2026-09-21-crop-audio-projects-annotations.md`.
- Code: `src/videotimeline.cpp`, `src/videoexporter.cpp`, `src/editorwindow.cpp` (Studio, playback bar,
  video item), `src/canvas.cpp`, `src/studiopopover.cpp`, `src/cropbar.cpp`, `src/theme.cpp`, `resources/eddy.qss`.
- Tests: `tests/test_editorwindow.cpp` (`chromeFollowsTheDensityRules`), `tests/test_theme.cpp` (`everyIconSharesOneGrid`).
- Tools: `tools/normalize_icons.py`.
- Boltsnap, read only: `src/record/cursor.rs` (sidecar writer; mellow now 60/15.5, quick 600/49; built-in arrow
  with motion blur since `ef8cf77`/`1c1f6b4`). Shelf offers only `video/mp4`, so GIF cards are unknown there.

## Key decisions / assumptions

- Studio stays optional. Without Studio the export path is unchanged. The UI stays grey, colour only in the export.
- Design answers:
  - Q1 C: 28 px zoom lane with the real zoom curve.
  - Q2 A: floating context bar at the bottom centre.
  - Q3 C: stay zoomed, drag the camera, mini map.
  - Q4 B: pages Style · Camera. Symbols only on Motion, drawn at runtime from the actual spring,
    at the usual 2.8 stroke (Q4d).
  - Q5 B: the timeline shows output time.
  - Q6 A: the Paper swatch stays as it is.
  - Q7 B/B2: an export popover at Save with segmented rows. Short click and Enter keep today's save.
- Scope answers:
  - N1 a: time windows only for Redact and Spotlight.
  - N2 a: audio plan unchanged, no captions.
  - N3 a: project core P5 right after the zooms.
  - N4 a: motion blur later and optional.
  - N5 a: suggestions are normal segments, one undo step; clicks are used once a track has them.
- Cursor: Boltsnap bakes it. Eddy only uses the track for Follow Cursor, suggestions and keep-zoomed-in.
- Performance: QPainter is enough, QRhi is not needed, but the three stages must overlap. Measured before
  planning: 1080p60 at 102 to 132 fps. 4K output runs at about 43 fps.
- Preview: readability first, at acceptable cost. The user wants both.
- Commits carry no AI co-author trailers. Nothing is pushed without approval. Builds use `--parallel 2`.
  Subagents only when the user asks. Boltsnap is read only. Never touch Hyprlock.

## Commands run and results

- Baseline: `cmake --build build-rel --parallel 2` exit 0, `ctest --test-dir build-rel` 28/28.
- After S0: 31/31. After S1: 33/33. After the preview fix, the icons and the throttle: 34/34 (37.5 s).
  `git diff --check` clean.
- Render-path spike (throwaway `/tmp/studio-probe`), 1080p60:
  - Serial pipes: 45 to 59 fps.
  - Overlapped pipes: 102 fps (x264), 122 to 132 fps (Vulkan).
  - 4K: 43 fps.
- S1 on the real clip (`boltsnap-2026-09-21_18-25-58.mp4`, 5 s, 2058×1190, hardware encoder):
  - Filter graph 2.47 to 2.51 s, renderer with a 2× zoom 2.81 to 2.86 s.
  - Renderer vs filter graph: mean difference 0.6/255.
- Mutation checks, all caught by the tests:
  - S0: wrong omega, wrong seam.
  - S1: camera ignored, BT.709 matrix dropped.
  - Icon: old QToolButton layout.
  - Preview: old paint path (subagent).
- Preview playback, offscreen, paired A/B under the same desktop load:
  - 240 fps clip at 45 %: subagent version 156 to 160 % CPU, now 79 to 94 %.
  - 60 fps clip at 72 %: about the same either way, 109 to 145 %.
  - Before any fix (nearest neighbour): about 101 % and 45 %.

## Open blockers / risks

- Not clicked in a real Wayland window: popups, the preview look on the real desktop, CPU with RHI conversion.
- 60 fps sources below 100 % zoom still cost more CPU than nearest neighbour did, because each frame needs the
  area filter. Offered next steps: filter on a worker thread, or a GPU path. The user has not decided.
- N6 spring values are not chosen yet. The comparison clips were in `/tmp/studio-springs/`, which does not survive.
- No real `*.cursor.json` on disk yet: `find ~ -name "*.cursor.json"` is empty.
- The earlier one-off `test_crop` watchdog hang was not seen again.
- The Boltsnap working tree belongs to another session. Do not touch it.

## Exact next steps

1. The user reviews the commits on `feat/studio` (`git log --stat main..feat/studio`).
2. After approval, push as the user decides (branch plus PR, or merge into `main`).
3. Ask for the N6 spring choice. Then write `docs/plans/2026-09-24-studio-s2-zooms.md` (S2: lane, context bar,
   canvas target with mini map, preview camera, Camera page, `SetStudioDocumentCommand`, export wiring,
   `eddy_preview` modes) and implement it with tests, including the preview-equals-export check (plan 7.2).
4. Then project core P5, then S4 fragments, following plan section 9.

## Useful resume commands

```sh
cd /home/mt/projects/eddy
git status --short --branch
git log --oneline main..feat/studio
cmake --build build-rel --parallel 2
ctest --test-dir build-rel
git diff --check
ffmpeg -v error -y -ss 5 -i ~/Bilder/boltsnap/boltsnap-2026-09-21_18-25-58.mp4 -frames:v 1 -vf scale=1280:-2 /tmp/studio-frame.png
QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-studio.png dark studio-open /tmp/studio-frame.png
QT_QPA_PLATFORM=offscreen build-rel/eddy_preview /tmp/eddy-video.png dark video-file ~/Bilder/boltsnap/boltsnap-2026-09-21_18-25-58.mp4
```

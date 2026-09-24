# Handoff: Video still visibility, performance and session close

Date: 2026-09-21
Project: `/home/mt/projects/eddy`

## Goal / current status

- Latest request: show the first video frame immediately after loading, while paused, without requiring Space to start playback. Implemented and built locally.
- The user then requested `$session-save` and a commit of all work so this session can close.
- At handoff preparation: branch `main`, HEAD `1c270a6`, clean index and working tree; two commits ahead of the locally recorded `origin/main`. This document is committed after that code state. No push is authorized for this closeout.
- `git branch --no-merged main` returned no branches. The existing local `feat/outfit-type-scale` points to `562744c`, already contained in `main`.
- Installed launcher `/home/mt/.local/bin/eddy` points to `/home/mt/projects/eddy/build-rel/eddy`. Rebuilt executable includes the fix. Existing windows retain their old process; open Eddy again to use the update.
- Final verification: 58 editor-window tests and 17 crop tests passed. A rendered screenshot from a real replay clip was visually inspected with the player paused at position zero. This is application-render verification, not a user confirmation that the original intermittent problem is gone.

Relevant commits:

| Commit | Content |
| --- | --- |
| `8e8f8e1` | Merged Crop work, including the visible tool and image/video output integration. |
| `8f82f1b` | Lazy video pixel conversion, explicit export preparation and hardware-assisted export with CPU fallback. |
| `562744c` | Concurrent UI session's Outfit/type/spacing update; also includes this session's paused-still implementation and initial regression test. |
| `1c270a6` | Corrected rendered-color assertions so white/empty surfaces cannot satisfy the red/blue checks. |

## Files changed

Latest video visibility fix:

- `src/editorwindow.h`
  - Added `m_videoStill` and `showVideoStill()`.
- `src/editorwindow.cpp`
  - Keeps a native-resolution pixmap of a valid paused frame as a child of the background video item. A backend surface clear cannot erase that still.
  - Pausing refreshes the still; the first valid playing frame hides it. Normal playback retains the video renderer instead of converting every frame for this feature.
  - Reuses `updateVideoBackground()` for orientation, sizing and pixel conversion. Invalid frames leave the last valid image intact.
  - The still is non-interactive and uses background z-order. Hiding the video parent also hides the still for annotation-only export and frame-copy composition.
  - Queues an initial pause retry after `LoadedMedia` only when no frame or explicit seek has arrived and the player is still stopped. Pending playback takes precedence.
- `tests/test_editorwindow.cpp`
  - Added `pausedVideoStaysVisibleBeforePlayback`: a generated red/blue H.264 clip with AAC audio and default animations checks the rendered first frame before playback, position zero, surface clearing, Space playback/pause, subsequent blue frame, frame copy and transparent annotation export.
  - Color checks require the expected channel to be high and the opposite channel low. A single-channel check also accepts white and was corrected in `1c270a6`.

Earlier performance work preserved in `8f82f1b`:

- `src/editorwindow.{cpp,h}`: convert frames to CPU images only when needed; edits invalidate cached output without automatically starting an export. Paused-still rendering now also requires a CPU image.
- `src/dragpill.{cpp,h}`: explicit `Prepare drag` action before exporting edited video, followed by `Drag out`; keyboard activation retained.
- `src/videoexporter.cpp`: actual cached capability probes for H.264 NVENC/Vulkan/VAAPI, CPU retry with x264 `veryfast`/CRF 18, bounded encoder work and omission of empty annotation overlays.
- `tests/test_editorwindow.cpp`, `tests/test_videoexporter.cpp`, `README.md`, `resources/eddy.qss`, `tools/eddy_preview.cpp`: accompanying behavior checks and presentation/documentation updates in that commit.

Concurrent UI changes were preserved, not recreated by the video fix. `562744c` contains bundled Outfit, device-pixel-aware icon rendering, menu arrow changes and a shared type/spacing scale across theme, toolbar, popovers, timeline and tests. Use that commit for the complete file list.

Closeout adds this handoff file. No other Eddy changes were pending when the handoff was prepared.

## Files inspected

- `src/main.cpp`: application theme setup and media/window startup.
- `src/editorwindow.{cpp,h}`: loading, frame presentation, playback state, seeking, export and frame-copy paths.
- `src/canvas.cpp`, `src/toolcontroller.cpp`: viewport rendering, crop foreground, panning and item interaction.
- `src/videopreviewprovider.cpp`: asynchronous thumbnail/hover decoding; kept separate from the full-resolution still.
- `src/videoexporter.cpp`, `src/dragpill.cpp`: existing performance and explicit-export behavior.
- `tests/test_editorwindow.cpp`, `CMakeLists.txt`, `README.md`: regression coverage, build targets and documented behavior.
- `docs/plans/2026-09-21-crop-audio-projects-annotations.md` and `docs/plans/2026-09-21-video-workflow-polish.md`: earlier accepted scope and remaining planned work.
- `/home/mt/projects/boltsnap` Git status, history and worktrees; replay crop settings in commit `2f88279`.

## Key decisions / assumptions

- Work on local `main`; do not create new branches or push without renewed authorization. The user explicitly declined changing GitHub protection and requested local commits. Avoid triggering CI/email noise.
- A concurrent session committed the UI update together with the still changes while this session was testing. Preserve this history; do not amend/rewrite it or claim its remote branch was created/pushed by this session. Local `main` was advanced to contain it, then the test correction was committed locally.
- Preserve the current compact grayscale design, icon geometry, alignment and typography. Current source and README supersede older plans' Noto Sans/fixed-rasterization values; those plans predate Outfit/device-pixel rendering.
- No autoplay/muted warm-up or extra full-resolution ffmpeg decoder was added for the still. Reuse the existing decoded frame and keep expensive conversion out of normal playback.
- Keep exports explicit. Earlier automatic background exports caused unacceptable resource use. Cap local build parallelism at two rather than launching broad concurrent builds.
- No subagents unless the user explicitly requests them. No AI co-author commit trailers.
- Files/directories may only be deleted with `/home/mt/.local/bin/safe-rm` after inspecting exact targets. Never bypass a refusal. Never execute, signal, preview or screenshot Hyprlock.

## Commands run and results

Latest change:

- `cmake --build build-rel --parallel 2 --target eddy test_editorwindow`: passed; updated the installed executable through its existing symlink.
- `QT_QPA_PLATFORM=offscreen build-rel/test_editorwindow pausedVideoStaysVisibleBeforePlayback`: final run passed. An earlier draft test failed its frame-copy assertion because loose color checks let a white viewport pass as red/blue; tightened checks resolved it.
- `QT_QPA_PLATFORM=offscreen build-rel/test_editorwindow`: **58 passed, 0 failed, 0 skipped**, approximately 14 seconds.
- `cmake --build build-rel --parallel 2 --target test_crop`: passed.
- `QT_QPA_PLATFORM=offscreen build-rel/test_crop`: **17 passed, 0 failed, 0 skipped**, approximately 6.6 seconds.
- `git diff --check`: passed.
- `git branch --no-merged main`: empty.
- Temporary probe `/tmp/eddy-still-probe.cpp` linked against `build-rel/libeddy_core.a`; opened a real replay with animations enabled and captured the editor after loading. Final probe reported `BufferedMedia`, `PausedState`, position `0`, valid frame with timestamp `17000` microseconds and no player error. `/tmp/eddy-still-after.png` was visually inspected and showed the video frame.

Temporary evidence, not durable dependencies: `/tmp/eddy-still-editor-tests.log`, `/tmp/eddy-still-crop-tests.log`, `/tmp/eddy-still-after-probe.log` and `/tmp/eddy-still-after.png`. The committed regression test generates its own input.

Earlier performance verification in this session context included the complete 26-target test suite and a short 1080p crop/export measurement (approximately 1.274 s before, 1.037 s after with a cold probe). Those results belong to the earlier performance change, not a fresh complete-suite or cross-platform run of the still fix.

## Open blockers / risks

- No outstanding build/test blocker for the latest fix. Windows was not rerun for it.
- The exact user-reported startup failure did not reproduce with the available clips before the change: they already decoded a paused frame. The implementation now retains a stable paused image and retries preroll after loading; do not describe the original backend failure as conclusively diagnosed.
- `QWidget::grab()` forces rendering. The inspected image proves the render path, but is not equivalent to observing an untouched compositor surface. User verification in a freshly opened Eddy window remains useful.
- Known non-failing local diagnostics: PipeWire format warnings, offscreen window-opacity warnings and CPU conversion fallback when no RHI backend is available.
- A parallel UI session worked in this same checkout, including Git operations. Recheck status before further work. No pending Eddy source edits remained at handoff preparation.
- Broader accepted plan is only partially implemented: **Crop is implemented; audio waveform/output mute, resumable projects/recovery, and annotation alignment/numbered-step helpers remain planned** in `docs/plans/2026-09-21-crop-audio-projects-annotations.md`. Do not call those features complete.
- Earlier GitHub CI disablement was intentional after repeated failure emails. Live workflow/protection state was not re-queried during this closeout; do not assume authorization to re-enable or relax it.

Related Boltsnap state, checked during closeout:

- `/home/mt/projects/boltsnap`: `main` at `252136f`, matching its locally recorded remote. Earlier replay quality/crop-speed fix `2f88279` is an ancestor. It restores replay quality and removes realtime pacing from cropped clip export (hardware QP 18; CPU `veryfast`/CRF 18).
- Earlier user-authorized daemon restart and installed replay binaries were completed in the prior work. Do not restart the current daemon again automatically: shelf contents can be lost.
- Boltsnap currently has **other-session, uncommitted changes** in `README.md`, `examples/selector_bench.rs`, `src/platform/linux/capture.rs`, `src/platform/linux/select_skia/mod.rs`, `src/platform/linux/shelf/font.rs`, `src/selector/mod.rs`, `src/selector/render.rs`, `src/selector/scene.rs`, plus untracked `src/selector/desktop.rs`. These were not authored or committed by this Eddy closeout.
- Detached worktree `/home/mt/projects/boltsnap-replay-fixes` remains at `2f88279`. Earlier safe-rm refused repository removal; it was left intact. Do not bypass that refusal.

## Exact next steps

1. This coding task is complete; no additional feature work is needed to close the session. Read this handoff and recheck Git status before resuming.
2. Open the rebuilt Eddy on the user's affected video and verify the first frame appears without Space. Then check pause, scrub and Crop. Do not close or replace an existing edited window without preserving its work.
3. If startup still fails, capture media status, playback state, first valid frame time and backend from that exact clip. Distinguish failure to decode from failure to paint; retain the test and avoid introducing autoplay or a costly second decoder by default.
4. For the next planned feature, resume at Audio in `docs/plans/2026-09-21-crop-audio-projects-annotations.md`, using current theme/source as the UI reference. Reconcile the detailed implementation with the user before expanding scope.
5. Keep commits local until the user requests publishing. Do not include the unrelated Boltsnap working-tree edits in an Eddy commit or blanket cleanup.

## Useful resume commands

```sh
cd /home/mt/projects/eddy
git status --short --branch
git log -5 --oneline
git branch --no-merged main
git diff --stat
git diff -- src/editorwindow.cpp src/editorwindow.h tests/test_editorwindow.cpp
readlink /home/mt/.local/bin/eddy
cmake --build build-rel --parallel 2 --target eddy test_editorwindow test_crop
QT_QPA_PLATFORM=offscreen build-rel/test_editorwindow
QT_QPA_PLATFORM=offscreen build-rel/test_crop
git -C /home/mt/projects/boltsnap status --short --branch
```

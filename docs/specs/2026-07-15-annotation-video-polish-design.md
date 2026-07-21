# Eddy Annotation and Video Polish Design

**Status:** Approved direction, 2026-07-15

## Goal

Make Eddy feel like a precise native annotation tool, then extend the same quality to video without turning it into a non-linear editor. Ship three independently useful releases: professional gestures, text plus Spotlight, and a fast frame-accurate video trim workflow.

## Product Boundary

Eddy remains the annotator. Boltsnap owns capture, history, temporary shelf storage, and capture lifecycle. Eddy owns retained annotations, preview, non-destructive trim state, rendering, and returning the finished file through the existing save/copy/drag/shelf routes.

The existing Qt Widgets, `QGraphicsScene`, `QUndoStack`, selection handles, and FFmpeg pipeline stay in place. No new UI toolkit, media framework, dependency, sidebar, or general tool architecture is introduced.

## Release 1: Professional Gestures and Interaction Polish

### Creation and Resize

- `Shift` constrains rectangles, ellipses, redactions, and Spotlight regions to 1:1. It snaps arrows to 45-degree increments.
- `Alt` draws and resizes rectangular tools symmetrically around their center.
- `Shift+Alt` combines both rules.
- Modifiers are evaluated on every pointer update so they can be pressed or released during a drag.
- Resize uses the same modifier meanings as creation.

### Selection and Movement

- Arrow keys move selected annotations by one source-image pixel.
- `Shift` plus an arrow key moves them by ten source-image pixels.
- `Shift` plus click toggles annotations in the current selection. Multiple selected annotations can move together; group resize is not included.
- `Alt` plus drag duplicates the selected annotation and moves the duplicate.
- `Ctrl+D` duplicates the selected annotations with a small visible offset.
- Every create, resize, move, duplicate, or delete gesture produces exactly one undo command.

### Navigation and Cancellation

- Holding `Space` temporarily pans the canvas without changing the active annotation tool.
- `0` fits the media to the viewport, `1` selects 100% zoom, and `+`/`-` zoom around the viewport center. Mouse-wheel zoom remains cursor-centered.
- `Esc` cancels the innermost active operation first: text edit, eyedropper, drawing gesture, or temporary pan. With no active operation, `Esc` closes Eddy.
- Pointer cursors and hover states reflect the currently available action, including creation, movement, handle resize, eyedropper, and pan.

### Theme Contract

- New controls use the existing mt-ui grayscale tokens, 14 px panel radii, 9 px control radii, brightness-only depth, and no structural borders or shadows.
- Dark and light palettes are both supported. The default follows the system; `theme=system|dark|light` provides an explicit config override without adding another toolbar button.

## Release 2: Text Tool and Spotlight

### Text Editing

- A click with the Text tool starts inline editing at that point.
- `Enter` inserts a newline. `Ctrl+Enter` or clicking outside commits. `Esc` restores the text and style from before the edit; an untouched new text item is removed.
- Double-clicking a text annotation re-enters editing.
- Text grows automatically until the user drags its right-side width handle. Once a width is set, text wraps inside it.
- Text remains retained and editable; export rasterizes it only in the final composite.
- One commit creates one undo entry rather than one entry per character.

### Text Controls

A compact contextual bar appears near the selected text item and follows the established Redact bar behavior. It contains only:

- font size,
- regular/bold,
- left/center/right alignment,
- plain or filled-label style.

The existing color control supplies the text or label color. Filled labels use a 9 px squircle and automatically choose readable neutral foreground text. Rich text, Markdown, arbitrary per-character formatting, and callout tails are not included.

### Spotlight

- Spotlight creates one retained focus region that dims the rest of the image or video with a neutral translucent overlay.
- The region can be rounded-rectangular or elliptical and uses the normal selection/resize gestures.
- A contextual bar offers shape plus three fixed intensity levels. There is no blur or glow.
- Spotlight is visible for the entire video, like every other annotation.
- A document has one Spotlight region. Drawing a new one replaces the previous region through an undoable command; multiple independent holes are not included.

## Release 3: Video Timeline, Trim, and Export

### Timeline

- The bottom playback strip becomes a dedicated timeline widget with play/pause, current time, total time, a playhead, and two trim handles.
- The timeline always represents the complete original file. Content outside the selected In/Out range remains visible but dimmed.
- A lightweight thumbnail contact sheet is generated asynchronously in memory. Failure falls back to the plain timeline without affecting editing.
- Dragging either trim handle seeks the preview to that boundary. Playback starts at In when necessary and stops at Out.
- `I` sets In and `O` sets Out. A visible reset control restores the full range without stealing `R` from the Rectangle tool. `J`, `K`, and `L` step backward, toggle playback, and step forward.
- The range must contain at least one video frame. Both the widget and exporter validate `0 <= in < out <= duration`.

### Annotation Semantics

- Trimming is non-destructive until save. The original source and excluded timeline sections stay available.
- Annotations remain global across the entire original video; there are no per-annotation time ranges.
- Saving exports only the selected continuous range with the same annotations. Split edits, removal of middle segments, transitions, and multiple output segments are not included.

### Fast, Exact Export

- A clean video with the full range selected continues to use the original file directly with no encode or copy.
- Any annotation or changed trim range produces one frame-accurate FFmpeg pass. Trim, overlay composition, timestamp reset, and audio handling happen in that same pass.
- The export preserves native dimensions and frame rate. H.264 uses `h264_nvenc` when it is actually available, with a high-quality `libx264` fallback. WebM keeps its compatible VP9/Opus path.
- Audio is trimmed to the same exact range and timestamps are reset so the exported clip starts at zero without A/V drift.
- Only one FFmpeg export may run at once. Changes are debounced; while a job runs, only the newest pending revision and trim range are retained.
- The cache identity includes annotation revision and In/Out points. Save, Copy, Drag-out, and Shelf reuse a matching completed export immediately.
- Dragging a trim handle never starts an export. Background preparation begins only after the handle is released and editing has been idle.

### Save and Failure Behavior

- The existing route priority remains unchanged: explicit output, Boltsnap card replacement, configured directory, then Shelf.
- The source file is never modified until a successful explicit same-path replacement is ready.
- A successful trimmed export is the file used by Save, Copy, Drag-out, and Boltsnap Shelf/Card replacement.
- Encoder, trim, or write failures keep the source and last valid cached export intact and surface one concise toast.
- Closing Eddy cancels its active export and removes only Eddy-owned temporary files.

## Testing and Verification

- Qt tests cover live modifier geometry, keyboard nudging, duplicate/multi-select movement, cancellation priority, and one-command undo behavior.
- Text tests cover multiline commit/revert, wrapping width, styling, rendering, and undo coalescing.
- Spotlight raster tests verify the focus region stays unchanged while the exterior dims at all three intensities.
- Timeline tests cover range clamping, handle crossing prevention, keyboard In/Out, reset, and playback boundaries.
- FFmpeg tests create short synthetic videos and verify exact output duration within one source frame, preserved dimensions/frame rate, overlay pixels, and synchronized audio duration.
- The complete CTest suite must pass. Real dark- and light-theme screenshots are inspected against the mt-ui no-border/no-shadow checklist.

## Explicit Non-Goals

- Multiple timeline segments or middle cuts
- Per-annotation video timing
- Transitions, audio editing, waveform editing, or effects
- Editable project-file persistence
- Magnetic guides or object snapping
- Group resize
- Rich text or multiple Spotlight holes

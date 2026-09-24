# eddy

A fast, minimal image and video annotation editor for Linux (Qt 6).

Linux is the supported platform. Windows builds are experimental and
community-maintained: CI verifies that they compile and pass automated tests,
but there is no human regression testing or official Windows release support.

Takes an image or video from a file (images also support stdin), lets you annotate it, then outputs the result to the clipboard, a file, stdout, or the Boltsnap shelf. Linux keeps the frameless floating workflow; the experimental Windows build uses native window controls and file dialogs.

---

## Tools

| Tool | Key | Description |
|------|-----|-------------|
| Move | `M` | Select and reposition any annotation |
| Arrow | `A` | Directional arrow |
| Pen | `P` | Freehand path |
| Rectangle | `R` | Stroked rectangle outline |
| Ellipse | `E` | Stroked ellipse outline |
| Highlight | `H` | Semi-transparent highlight band |
| Text | `T` | Inline text with wrapping, alignment, size, bold and filled-label styles |
| Step | `N` | Numbered circles 1, 2, 3 in the stroke colour; the bar sets the number and S/M/L and renumbers all steps in the order they were made |
| Redact | `X` | Draw a redaction region; a floating mode-bar lets you switch between **Blur / Blacken / OCR-Blur / OCR-Blacken** |
| Spotlight | — | Keep one rounded or oval focus region bright while dimming the surrounding canvas |
| Crop | `C` | Set the visible image or video area, with eight handles and aspect presets |

Every annotation is a retained scene item — select and move it with the Move tool. Full undo/redo. Crisp anti-aliased rendering via Qt's QGraphicsView.
Moved items snap to the edges and centres of other items and of the picture, with thin guides while
dragging; hold `Ctrl` to place freely, or turn **Snap to objects** off in the canvas context menu.
With two or more items selected a bar lines them up (left, centre, right, top, middle, bottom) and,
from three on, spaces them evenly; each is one undo step.

The interface uses Vis-style grayscale surfaces, bundled Outfit typography (SIL OFL) on an 11/13px scale, rounded
controls, and grouped monochrome tool icons with tooltips (tool name + hotkey).
Tools sit to the left of the canvas without a surrounding panel. The flat top bar
holds undo/redo, stroke controls and output actions, with compact 6px state fills
matching Vis. **Fit** and the live zoom percentage (click for 100%) sit at the
bottom left; **Drag out** stays centered at the bottom. The footer and timecodes use
your system's fixed-width font. The window title includes
the filename and dimensions.
Save and Copy are icon-only with tooltips; the labeled **To shelf** action
keeps its card-plus icon, and Drag out has its own grip icon. Checked controls
fill a 22 px squircle. The floating bars over the canvas run one step larger,
with 24 px controls and 20 px glyphs. The text bar spells its size and bold
controls as S / M / L and B, drawn as monoline letterforms on the icon grid
rather than set in a typeface, so they carry the stroke weight of their
neighbours.

Every icon sits on one keyline grid: ink centred in the 24 unit viewBox,
reaching a 20 unit live area, at a 2.8 stroke. `tools/normalize_icons.py`
re-fits an icon to the grid and a test in `test_theme` enforces it. Dark and light themes are available from
the toolbar.

**Crop** has a visible button at the bottom of the tool rail. Drag a handle to
resize, drag inside to move the area, or drag outside to draw a new one. Its
floating bar offers Free, Original, 16:9, 9:16 and 1:1, the output dimensions,
Reset, Cancel and Apply. Shift keeps the current ratio; Alt resizes from the
center. Enter applies one undo step; Escape first cancels a drag, then the crop.
Middle-drag and Space-drag still pan. Reopening Crop reveals the original image;
annotations retain their original positions. Save, Copy, frame copy, drag and
shelf delivery use the same crop. Video crop coordinates align to even pixels.
Orthogonal video rotations and non-square pixels use display coordinates;
unsupported display transforms show an explanation when Crop is selected.

**Studio** (top bar, beside the output actions) is optional framing for images
and videos: a background (none, six gradient presets or your own image) with
padding, rounded corners, a soft shadow and an output ratio (Auto, 16:9, 4:3,
1:1, 9:16; the background grows, the content is never cropped); the popover
shows the resulting output size. It is off for
every new document. Switching it on restores the style you used last, stored in
the config file's `[studio]` group, and one popover session is one undo step.
The canvas previews exactly what Save, Copy, drag, shelf and frame copy deliver.
Videos keep their frame rate and audio.

**Zooms** (videos): with Studio on, a zoom lane sits under the filmstrip and draws
the camera's real zoom over time. Click the empty lane or press `Z` to add a
two-second 2× zoom, drag it to move, drag its edges to resize (it snaps to the
playhead, the trim and its neighbours). A selected zoom shows where the camera
comes to rest: drag empty content with the Move tool, or the window in the mini
map, to aim it; the wheel over the map sets any level from 1.1× to 4×. The bar
under the canvas sets 1.25×, 1.5×, 2× or 3×, Focused, Smooth or Instant motion,
and removes the zoom. The popover's **Camera** page sets the motion of every zoom
and **Keep zoomed in**, which fills a narrow output ratio such as 9:16 with a
window of the video instead of background. Playback shows the camera ride, and the
export matches the preview; zooms export through a frame renderer at 60 fps,
everything else keeps the ffmpeg filter-graph export. When a video has a Boltsnap
cursor track beside it (`clip.cursor.json`), zooms can follow the pointer,
**Keep zoomed in** follows it until you place the view by hand, and **Suggest zooms**
on the Camera page proposes zooms where the pointer clicks or rests; the cursor itself
stays the one Boltsnap baked in. **Blur** on the Camera page smears camera moves in the
export only; the preview stays sharp. **Presets** save a Studio look, apply it and share it
as a file, background image included.

`S` splits a video at the playhead. A selected fragment can be cut, restored, sped up or
slowed down, or joined with the one before; the timeline then shows the edited time and a
cut becomes a notch in the ruler. Redactions and spotlights on a video can show from the
playhead on; their stretch sits on a mask lane under the timeline, where it moves and
resizes. Videos with sound get a waveform lane right under the filmstrip (its context menu
hides it); **Include audio in output** in the speaker menu leaves the sound out of the saved
video, shown as **No audio** beside the speaker, and undoes like any edit.

The export popover (hold **Save**) picks a preset (Original, Web, Small, GIF), the format,
the size and the frame rate, and its footer saves or opens a **project**: a `.eddy` file
with an `.eddy.assets` folder holding a copy of the original, so every layer stays editable.
Edits are also kept on their own while you work (up to 2 GB, in
`~/.local/share/eddy/recovery`); **Resume…** there or `eddy --resume` brings them back.

Video has an adaptive filmstrip and a time ruler. Hover for a source-frame preview,
drag to scrub, or pull the end grips to trim. Hold **Shift** for fine trim; **Esc**
cancels the drag. The **Start / End** labels and inward-facing brackets identify
the kept range. Each completed trim is one undo step. **Start / End** (`I` / `O`)
sets the range at the playhead, and the time fields accept seconds, `m:ss.mmm` or
`h:mm:ss.mmm`. The selected duration is shown beside them.

**Ctrl+wheel** zooms the timeline around the pointer; **Shift+wheel** or horizontal
scrolling pans it. Its context menu offers Zoom and Fit. With timeline focus,
`+`, `-`, `0` operate on the timeline. Elsewhere they still control the canvas.
Scrubbing pauses playback and resumes inside the selection if it was playing;
trimming stays paused. Frame stepping uses nominal fps; variable-rate clips remain
time-based.

Loop repeats the selected range. The speed menu offers **0.25×–2×** for preview
only; exported video keeps its original speed and audio. Hold the **Copy** button
to open **Copy current frame** (`Ctrl+Shift+C`), including annotations and redactions.
Normal Copy still delivers the video. A pending seek finishes before its frame is
copied. In narrow windows the trim fields get their own row; hold the speaker
button for volume. Keyboard users can open these menus with **Alt+Down**.
The canvas shows images and video at any zoom without moiré: shrunk views are
area-filtered, views up to 2× are smoothed and from 2× on pixels stay crisp.
Playback converts each frame once and no more often than the screen refreshes.
**Drag out** and a quiet export status remain at the bottom; while exporting it
shows the progress and a Cancel button, and a stalled encoder is stopped with its
error shown. Editing and playback
do not start an export. For an edited video, click **Prepare drag** once, then
drag the ready file out. Save, Copy and Shelf prepare the video when requested;
they reuse the result until you make another edit.
H.264 export uses a hardware encoder after a real capability check and retries
with the CPU encoder if necessary. Pure trim/crop exports skip the empty
annotation layer. Playback only materializes CPU images for tools that need
their pixels, or when copying a frame.

**Toolbar controls:**

| Control | Description |
|---------|-------------|
| ↶ / ↷ | Undo / Redo buttons (same as `Ctrl+Z` / `Ctrl+Shift+Z`) |
| **S / M / L** | Line-width chooser: 2 px / 4 px / 8 px stroke |
| Colour swatch | Opens a **colour popover** with the current hex value, marked presets, **More colours…** and **Pick from image** |
| Dark / Light | Switches theme immediately and remembers the choice |
| Copy | Copies the edited image to the clipboard (same as `Ctrl+C`) |
| Shelf button | Sends the current edited image to the Boltsnap shelf as a new card |

With the **Move tool**, selecting a shape (Rectangle, Ellipse, Highlight, Redact, Spotlight) shows **8 drag handles** to resize it. Selecting an Arrow shows **2 endpoint handles**. Text shows one width handle for wrapping; Pen is move-only.

With the **Text tool**, drag existing text to move it, double-click it to edit, or click empty canvas space to create a new text annotation.

### Redaction and OCR

Linux uses `tesseract` from `PATH` and requires the language selected by
`ocr_lang`. The community Windows installer scripts can bundle the OCR runtime
and German language data; CI preview artifacts do not.

On video, Blur is applied frame-by-frame during export. OCR detects text in the
currently displayed frame and keeps those redaction rectangles fixed for the clip;
it does not track moving text.

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `A` `P` `R` `E` `H` `T` `N` `X` `M` | Switch tool |
| `Ctrl` while moving | Place freely, without snapping |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Shift` while drawing/resizing | Constrain proportions; snap arrows to 45° |
| `Alt` while drawing/resizing | Draw or resize from the centre |
| `Shift`-click | Add/remove an annotation from the selection |
| Arrow keys / `Shift`+Arrow keys | Move the selection by 1 px / 10 px |
| `Ctrl+D` / `Alt`-drag | Duplicate the selection |
| `Enter` while editing text | Insert a new line |
| `Ctrl+Enter` while editing text | Commit the text edit, clear its selection and return focus to the canvas |
| `Esc` while editing text | Revert the edit; a new untouched text box is removed |
| `Delete` / `Backspace` | Remove the selection (one undo step) |
| `Enter` | Save (replace source card, use explicit/configured output, or return to shelf) |
| `Ctrl+S` | Save |
| `Ctrl+C` | Copy to clipboard |
| `Ctrl+Shift+C` on video | Copy the displayed annotated frame |
| Tap `Space` / `K` on video | Play / Pause |
| `J` / `L` on video | Pause and step backward / forward |
| `I` / `O` on video | Set Start / End at the playhead |
| `S` on video | Split at the playhead |
| `Z` on video | Add a zoom at the playhead |
| `Enter` / `Esc` in a trim time field | Apply / restore its value |
| `C`, then `Enter` / `Esc` | Open Crop, apply / cancel |
| `Z` on video | Add a zoom at the playhead |
| `Delete` / Left / Right with a zoom selected | Remove it / move it by a frame (`Shift`: ten) |
| `Esc` | Cancel the active interaction, then close |
| Scroll wheel / `+` / `-` | Zoom |
| `0` / `1` | Fit image / 100% zoom |
| Middle-drag / hold `Space` and drag | Pan |

---

## Usage

```
eddy IMAGE
eddy -f IMAGE
```

`IMAGE` can be `-` to read from stdin.

### Options

| Flag | Description |
|------|-------------|
| `-f, --file IMAGE` | Input image (`-` = stdin) |
| `-o, --output PATH` | Write PNG to file (`-` = stdout) |
| `--save-dir DIR` | Directory for the in-editor save action |
| `--copy` / `--no-copy` | Copy result to clipboard (default: copy) |
| `--tool NAME` | Start with a specific tool active (e.g. `--tool redact`; legacy `blur`/`pixelate` map to Redact) |
| `--early-exit` | Exit after the first save |
| `--no-anim` | Disable all animations (window fade, smooth zoom, commit fade-in) |
| `--config PATH` | Alternate config file |

swappy-compatible aliases (`-f`, `-o`, `--early-exit`) are supported, so replacing `swappy` with `eddy` in existing keybinds and scripts works without changes.

### Default save behavior

Save uses this priority: explicit `-o` / `--save-dir`, replacement of a supplied Boltsnap card, configured `save_dir`, then shelf return. If `copy_on_save` is enabled, the result is also copied to the clipboard; if Boltsnap is unavailable, Eddy falls back to clipboard copy.

### Pipeline examples

```sh
# Wayland screenshot → eddy
grim -g "$(slurp)" - | eddy -f -

# With boltsnap
boltsnap area --no-copy -o - | eddy -f -
```

---

## Configuration

`~/.config/eddy/config` — INI format, `[eddy]` group.

| Key | Description |
|-----|-------------|
| `default_tool` | Tool to activate on start |
| `line_width` | Default stroke width |
| `save_dir` | Default save directory (unset means shelf return) |
| `text_font` | Font for the Text tool |
| `stroke_color` | Default stroke color |
| `early_exit` | Exit after first save (`true`/`false`) |
| `copy_on_save` | Copy to clipboard on save (`true`/`false`) |
| `animations` | Enable window/tool animations (default: `true`) |
| `ocr_lang` | Tesseract language(s) used by OCR redaction (default: `deu`) |
| `ocr_psm` | Tesseract page-segmentation mode (default: `6`) |
| `theme` | `system` (default), `dark`, or `light` |

---

## Install

On Linux, build from source with the Qt packages supplied by your distribution.

The CMake install also includes a desktop entry and icon. For a per-user install
after building, with `~/.local/bin` on `PATH`:

```sh
cmake --install build-rel --prefix "$HOME/.local"
update-desktop-database "$HOME/.local/share/applications"
xdg-mime default eddy.desktop image/png video/mp4 video/webm video/x-matroska video/quicktime video/x-msvideo
```

This makes Eddy the desktop default for PNG images and common video formats,
including clicks on Boltsnap shelf cards. Check it with
`xdg-mime query default image/png` or `xdg-mime query default video/mp4`. Boltsnap opens
these through `xdg-open` without a card ID, so the default Save action returns a
new shelf card.

There are no official Windows releases. CI publishes experimental, untested
portable preview artifacts for contributors; Windows support is best-effort
and community-maintained.

Video editing and export need `ffmpeg`/`ffprobe` on `PATH` on every platform.
Windows preview artifacts and installer scripts do not bundle them. Image
annotation works without them.

---

## Build

Requires Qt 6 Widgets, Multimedia, and SVG. The experimental Windows build
additionally uses Qt Network for Boltsnap named-pipe IPC.

```sh
# Debug (default)
cmake -S . -B build
cmake --build build --parallel 3

# Run tests
ctest --test-dir build --parallel 3 --output-on-failure

# Release
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --parallel 3
```

Community Windows builds use a Qt 6 MSVC kit and Visual Studio 2022. Launching
`eddy.exe` without arguments opens the native media picker. MSI and NSIS
installers can still be produced after the Release build with:

```powershell
.\packaging\windows\build-msi.ps1 -BuildDirectory build-win -QtDirectory C:\Qt\6.8.3\msvc2022_64 -TesseractDirectory C:\path\to\ocr-runtime -Version 1.0.3
.\packaging\windows\build-nsis.ps1 -BuildDirectory build-win -QtDirectory C:\Qt\6.8.3\msvc2022_64 -TesseractDirectory C:\path\to\ocr-runtime -Version 1.0.3
```

Both installers install Eddy per machine, add a Start-menu shortcut, and register
the classic **Open with Eddy** action. They do not install certificates, MSIX
identity, or Explorer COM extensions. Building the NSIS format requires NSIS 3.

---

## Known limitations

- **Pen** annotations are move-only; resize handles are not supported for that item type.

---

## License

[MIT](LICENSE).

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
| Redact | `X` | Draw a redaction region; a floating mode-bar lets you switch between **Blur / Blacken / OCR-Blur / OCR-Blacken** |
| Spotlight | — | Keep one rounded or oval focus region bright while dimming the surrounding canvas |

Every annotation is a retained scene item — select and move it with the Move tool. Full undo/redo. Crisp anti-aliased rendering via Qt's QGraphicsView.

The interface uses Vis-style grayscale surfaces, Noto Sans typography, rounded
controls, and grouped monochrome tool icons with tooltips (tool name + hotkey).
Tools sit to the left of the canvas without a surrounding panel. The flat top bar
holds undo/redo, stroke controls and output actions, with compact 6px state fills
matching Vis. **Fit** and the live zoom percentage (click for 100%) sit at the
bottom left; **Drag out** stays centered at the bottom. The footer uses MonoLisa
with a monospace fallback. The window title includes
the filename and dimensions.
The labeled **To shelf** action uses a card-plus icon; Save and Drag out have
distinct disk and grip icons. Dark and light themes are available from the toolbar.

Video uses a full-width filmstrip above one compact transport row. Drag the muted
end grips to trim, click the strip to seek, or use **In / Out** (`I` / `O`) to set
the range at the playhead. Excluded frames fade into the background; precise trim
times, playback and volume sit below the strip. **Drag out** remains at the bottom.

**Toolbar controls:**

| Control | Description |
|---------|-------------|
| ↶ / ↷ | Undo / Redo buttons (same as `Ctrl+Z` / `Ctrl+Shift+Z`) |
| **S / M / L** | Line-width chooser: 2 px / 4 px / 8 px stroke |
| Colour swatch | Opens a **colour popover** with the current hex value, marked presets, **More colours…** and **Pick from image** |
| Dark / Light | Switches theme immediately and remembers the choice |
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
| `A` `P` `R` `E` `H` `T` `X` `M` | Switch tool |
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

# eddy

A fast, minimal image-annotation editor for Linux (Qt6) — a standalone swappy replacement.

Takes an image (from a file or stdin), opens a frameless floating editor, lets you annotate it, then outputs the result to the clipboard, a file, or stdout. Independent of any capture tool; meant to be dropped in place of swappy in keybinds and scripts.

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
| Text | `T` | Text stamp with inline edit |
| Redact | `X` | Draw a redaction region; a floating mode-bar lets you switch between **Blur / Blacken / OCR-Blur / OCR-Blacken** |

Every annotation is a retained scene item — select and move it with the Move tool. Full undo/redo. Crisp anti-aliased rendering via Qt's QGraphicsView.

The toolbar shows **tool icons with tooltips** (tool name + hotkey) — no letter labels.

**Toolbar controls:**

| Control | Description |
|---------|-------------|
| ↶ / ↷ | Undo / Redo buttons (same as `Ctrl+Z` / `Ctrl+Shift+Z`) |
| **S / M / L** | Line-width chooser: 2 px / 4 px / 8 px stroke |
| Colour swatch | Opens a **colour popover** with preset swatches and a *Custom…* entry that opens the full colour dialog |
| Shelf button | Sends the current edited image to the Boltsnap shelf as a new card |

If the window is made very short, the toolbar **auto-hides** and reappears when the cursor moves to the top edge, keeping the image at full height.

With the **Move tool**, selecting a shape (Rectangle, Ellipse, Highlight, Redact) shows **8 drag handles** to resize it. Selecting an Arrow shows **2 endpoint handles**. Pen and Text are move-only.

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `A` `P` `R` `E` `H` `T` `X` `M` | Switch tool |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Delete` / `Backspace` | Remove selected annotation (undoable) |
| `Enter` | Save (send image to Boltsnap shelf by default; write file only with `-o` / `--save-dir`) |
| `Ctrl+S` | Save |
| `Ctrl+C` | Copy to clipboard |
| `Esc` | Close |
| Scroll wheel | Zoom |
| Middle-drag | Pan |

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
| `--no-anim` | Disable all animations (window fade, smooth zoom, sliding pill, commit fade-in) |
| `--config PATH` | Alternate config file |

swappy-compatible aliases (`-f`, `-o`, `--early-exit`) are supported, so replacing `swappy` with `eddy` in existing keybinds and scripts works without changes.

### Default save behavior

Pressing `Enter` sends image edits to the Boltsnap shelf by default. If `copy_on_save` is enabled, the image is also copied to the clipboard; if the shelf is unavailable, Eddy falls back to clipboard copy. A file is written only when `-o FILE`, `-o -`, or `--save-dir` is given.

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
| `save_dir` | Default save directory |
| `text_font` | Font for the Text tool |
| `stroke_color` | Default stroke color |
| `early_exit` | Exit after first save (`true`/`false`) |
| `copy_on_save` | Copy to clipboard on save (`true`/`false`) |
| `animations` | Enable window/tool animations (default: `true`) |

---

## Build

Requires Qt 6 with Widgets, SVG and Multimedia. Linux runs Wayland-native or via XWayland; Windows uses the same editor and Qt rendering code without a separate design.

```sh
# Debug (default)
cmake -S . -B build
cmake --build build

# Run tests
ctest --test-dir build

# Release
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel
```

### Windows

Use Visual Studio 2022, CMake and a Qt 6 MSVC x64 installation containing Multimedia:

```powershell
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.9.3\msvc2022_64
cmake --build build-win --config Release
ctest --test-dir build-win -C Release --output-on-failure
cmake --install build-win --config Release --prefix "$PWD\dist\eddy"
```

The install step deploys Eddy and its required Qt runtime files. `ffmpeg`,
`ffprobe` and `tesseract` remain optional external tools for video export/probe
and OCR.

To launch this build from Boltsnap for Windows, point Boltsnap at the deployed
executable:

```powershell
$env:BOLTSNAP_EDITOR = "$PWD\dist\eddy\bin\eddy.exe"
boltsnap area --edit
```

Build either standalone Windows installer after the Release build:

```powershell
.\packaging\windows\build-msi.ps1
.\packaging\windows\build-nsis.ps1
```

The MSI is written to `dist\msi`; the NSIS setup executable is written to `dist\nsis`.
Both English setup wizards show the target directory and installation progress,
request elevation, install Eddy into Program Files and create a Start menu shortcut.
They also add **Open with Eddy**
to the primary Windows 11 image context menu through a packaged Explorer command,
keep a classic-menu fallback and register Eddy under Windows' **Open with** and
**Default apps** lists for common image formats. Starting Eddy without a file
opens the native Windows file picker.

Boltsnap shelf launches also pass an internal `--boltsnap-card-id` argument.
Eddy uses the same framed protocol as Boltsnap and talks to its per-user Windows
Named Pipe, so saving refreshes the originating shelf card. Standalone file,
stdin/stdout, clipboard, save and drag-out workflows remain available.

---

## Known v1 limitations

- **Text** editing is single-box stamp with inline edit; no multi-line layout.
- **Pen** annotations are move-only; resize handles are not supported for that item type.

---

## License

MIT. A `LICENSE` file may be added in a future release.

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
| Blur | `B` | Box blur region |
| Pixelate | — | Pixelate region (via `--tool pixelate`) |
| Redact | `X` | Solid redaction block |

Every annotation is a retained scene item — select and move it with the Move tool. Full undo/redo. Crisp anti-aliased rendering via Qt's QGraphicsView.

The toolbar shows **tool icons with tooltips** (tool name + hotkey) — no letter labels.

---

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `A` `P` `R` `E` `H` `T` `B` `X` `M` | Switch tool |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Enter` | Save (copy to clipboard; also write file if `-o` / `--save-dir` is set) |
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
| `--tool NAME` | Start with a specific tool active (e.g. `--tool blur`) |
| `--early-exit` | Exit after the first save |
| `--no-anim` | Disable all animations (window fade, smooth zoom, sliding pill, commit fade-in) |
| `--config PATH` | Alternate config file |

swappy-compatible aliases (`-f`, `-o`, `--early-exit`) are supported, so replacing `swappy` with `eddy` in existing keybinds and scripts works without changes.

### Default save behavior

Pressing `Enter` copies the result to the clipboard. A file is written only when `-o FILE`, `-o -`, or `--save-dir` is given.

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

Requires Qt 6 (Widgets). Linux-first; runs Wayland-native or via XWayland.

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

---

## Known v1 limitations

- **Pixelate** is reachable only via `--tool pixelate`; there is no toolbar button or hotkey yet.
- The **Move tool** repositions items but has no resize handles yet.
- **Text** editing is single-box stamp with inline edit; no multi-line layout.

---

## License

MIT. A `LICENSE` file may be added in a future release.

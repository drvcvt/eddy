# eddy

A small, fast editor for marking up screenshots and screen recordings on Linux.

Take a shot, draw an arrow, blur the token you almost leaked, press Enter. eddy
opens what you give it (a file, or an image on stdin), lets you annotate it, and
hands the result back: to the clipboard, a file, stdout, or the
[Boltsnap](https://github.com/drvcvt/boltsnap) shelf. It speaks swappy's flags,
so it drops into the keybinds you already have.

Videos get the same tools plus a timeline: trim, cut, speed up, zoom in on what
matters, and export with sound.

## What it does

**Annotate.** Arrows, pen, rectangles, ellipses, highlights, text with labels,
and numbered steps that renumber themselves. Everything stays an object you can
select, move, resize and undo. Moved items snap to each other and to the
picture, and a bar lines several up or spaces them evenly.

**Redact.** Blur or blacken a region, or let OCR find the text inside it and
cover only that. A spotlight dims everything but the part you care about.

**Crop.** Free or to a ratio (16:9, 9:16, 1:1, the original), without losing the
annotations outside it.

**Video.** The clip as pieces on a film strip, with a ruler and hover previews.
Split at the playhead, drag any piece's edge to trim it, take a piece out or play
it faster; the strip always shows exactly what gets exported. Redactions and spotlights can hold
for a stretch of the clip. Copy a single annotated frame, or the whole edited
video.

**Studio.** Optional framing for images and videos: a gradient or your own
background, padding, rounded corners, a shadow, and an output ratio. On videos,
zooms follow the pointer (with a Boltsnap cursor track) and can be suggested
where you clicked. Save a look as a preset and share it.

**Keep your work.** Save a project (`.eddy`) to come back to every layer later.
Edits are also kept as you go, and `eddy --resume` brings them back after a
crash or a closed window.

## Get it

eddy builds from source with Qt 6 (Widgets, SVG, Multimedia) and CMake:

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel --parallel 4
cmake --install build-rel --prefix "$HOME/.local"
```

The install adds a desktop entry and icon. To make eddy the default for images
and recordings (Boltsnap's shelf cards open through it too):

```sh
update-desktop-database "$HOME/.local/share/applications"
xdg-mime default eddy.desktop image/png video/mp4 video/webm video/x-matroska video/quicktime
```

Video needs `ffmpeg` and `ffprobe` on `PATH`; OCR redaction needs `tesseract`
with the language you set as `ocr_lang`. Images work without either.

Tests: `ctest --test-dir build-rel --output-on-failure`.

## Use it

```sh
grim -g "$(slurp)" - | eddy -f -          # a region, straight from the screen
boltsnap area --no-copy -o - | eddy -f -  # or through Boltsnap
eddy recording.mp4                         # a video
eddy --resume                              # pick up where you left off
```

| Flag | |
|------|---|
| `-f, --file PATH` | Input image or video (`-` reads an image from stdin) |
| `-o, --output PATH` | Write the result to a file (`-` for stdout) |
| `--save-dir DIR` | Save into this folder with a timestamped name |
| `--copy` / `--no-copy` | Copy the result to the clipboard, or don't |
| `--tool NAME` | Start with this tool |
| `--early-exit` | Quit after the first save |
| `--no-anim` | No animations |
| `--config PATH` | Use another config file |

Save goes to the first that applies: `-o` or `--save-dir`, the Boltsnap card it
was opened from, the configured `save_dir`, then the shelf. Hold **Save** for
export options: format, size, frame rate, GIF, and projects.

## Keys

`?` or `F1` (or the keyboard button in the top bar) shows all of them in the editor. Buttons with a
small rounded corner at their bottom right open more options on a right-click.

| Key | |
|-----|---|
| `M` `A` `P` `R` `E` `H` `T` `N` `X` `C` | Move, Arrow, Pen, Rectangle, Ellipse, Highlight, Text, Step, Redact, Crop |
| `Enter` / `Ctrl+S` | Save |
| `Ctrl+C` / `Ctrl+Shift+C` | Copy / copy the current video frame |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo |
| `Delete` | Remove the selection |
| `Ctrl+D`, or `Alt` and drag | Duplicate |
| Arrow keys, with `Shift` | Nudge by 1 px, by 10 px |
| `Shift` while drawing | Keep proportions, arrows at 45° |
| `Alt` while drawing | Draw from the centre |
| `Ctrl` while moving | Place freely, without snapping |
| Wheel, `+` `-` | Zoom |
| `0` / `1` | Fit / 100 % |
| Middle-drag, or hold `Space` and drag | Pan |
| `Esc` | Cancel what's in progress, then close |

On videos:

| Key | |
|-----|---|
| `Space` or `K` | Play / pause |
| `←` / `→` or `J` / `L` | Step back / forward one frame (cuts are skipped) |
| `Shift+←` / `Shift+→` | A second back / forward, as the clip plays |
| `↑` / `↓` | To the previous / next edge (piece, zoom, mask) |
| `Home` / `End` | To the first / the last frame of the clip |
| `I` / `O` | Set start / end at the playhead |
| `S` or Split | Split at the playhead |
| Click a piece | Choose it (its speed, join, delete) |
| Drag a piece's edge | Trim it, inner edges too (`Shift`: finer); the gap closes on release |
| `Shift` + drag a piece | Move it (and what follows) to leave a pause before it; it snaps back within 8 px (`Ctrl`: no snapping) |
| `Shift` + double-click a pause | Close it |
| Right-click a pause | Hold the last frame, or black |
| `Alt` + drag on the timeline, or `Shift+I` / `Shift+O` | Mark a range |
| `Delete` or Delete | Take the marked range or the chosen piece out |
| Click on a cut's notch | Restore the cut |
| Right-click the timeline › Cut pauses | Cut every stretch where nothing happens (or play it at 4×) |
| `Z` | Add a zoom at the playhead |
| Wheel, `Shift` + wheel | Zoom / pan the timeline |

## Configure

`~/.config/eddy/config`, an INI file with an `[eddy]` group:

| Key | |
|-----|---|
| `default_tool` | The tool eddy starts with |
| `line_width` | Stroke width |
| `stroke_color` | Stroke colour |
| `text_font` | Font for the Text tool |
| `save_dir` | Where Save writes (unset: back to the shelf) |
| `copy_on_save` | Also copy on save (`true` / `false`) |
| `early_exit` | Quit after the first save |
| `animations` | `true` (default) or `false` |
| `theme` | `system` (default), `dark` or `light` |
| `ocr_lang` | Tesseract languages for OCR redaction (default `deu`) |
| `ocr_psm` | Tesseract page segmentation mode (default `6`) |

## Windows

There is an experimental Windows build with native window controls and
installers (`packaging/windows`, Qt 6 MSVC kit). It gets far less testing than
Linux and nobody uses it day to day, so expect rough edges.

## License

[MIT](LICENSE). The bundled Outfit font is under the [SIL Open Font License](resources/fonts/OFL.txt).

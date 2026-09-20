"""Centre each icon's ink in its 24x24 viewBox, optionally scaling it to a
common live area. Measures with rsvg-convert and iterates until it converges."""
import subprocess, io, re, sys, pathlib
from PIL import Image

N = 240           # render 10x the viewBox so 0.1 units are measurable
BOX = 24.0

def ink(svg_text):
    png = subprocess.run(["rsvg-convert", "-w", str(N), "-h", str(N), "/dev/stdin"],
                         input=svg_text.encode(), capture_output=True).stdout
    a = Image.open(io.BytesIO(png)).convert("RGBA").split()[3]
    b = a.getbbox()
    if not b: return None
    x0, y0, x1, y1 = [v / (N / BOX) for v in b]
    return x0, y0, x1, y1

def wrap(body, dx, dy, s):
    t = f'translate({dx:.4f} {dy:.4f})' + (f' scale({s:.5f})' if abs(s - 1) > 1e-6 else '')
    return f'<g transform="{t}">{body}</g>'

def restroke(body, factor):
    return re.sub(r'stroke-width="([\d.]+)"',
                  lambda m: f'stroke-width="{float(m.group(1)) * factor:.4f}"', body)

def normalize(path, live=None, stroke=None):
    src = pathlib.Path(path).read_text()
    head, body, tail = re.match(r'(<svg[^>]*>)(.*)(</svg>)', src, re.S).groups()
    core = body
    if stroke is not None:                      # set every stroke to one rendered weight
        widths = {float(w) for w in re.findall(r'stroke-width="([\d.]+)"', core)}
        if widths:
            core = re.sub(r'stroke-width="[\d.]+"', f'stroke-width="{stroke}"', core)
    dx = dy = 0.0; s = 1.0
    for _ in range(24):
        cand = head + wrap(restroke(core, 1 / s), dx, dy, s) + tail
        b = ink(cand)
        if b is None: return None
        x0, y0, x1, y1 = b
        w, h = x1 - x0, y1 - y0
        dx -= ((x0 + x1) / 2 - BOX / 2)
        dy -= ((y0 + y1) / 2 - BOX / 2)
        if live:
            s *= live / max(w, h)
        if abs((x0 + x1) / 2 - BOX / 2) < 0.02 and abs((y0 + y1) / 2 - BOX / 2) < 0.02 \
           and (not live or abs(max(w, h) - live) < 0.05):
            break
    return head + wrap(restroke(core, 1 / s), dx, dy, s) + tail + "\n"

if __name__ == "__main__":
    live = float(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] != "-" else None
    stroke = float(sys.argv[2]) if len(sys.argv) > 2 and sys.argv[2] != "-" else None
    for f in sys.argv[3:]:
        out = normalize(f, live, stroke)
        if out: pathlib.Path(f).write_text(out); print("normalised", f)
        else: print("SKIP (no ink)", f)

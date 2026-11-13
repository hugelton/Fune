#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The app's icons from the owner's drawing (2026-11-13: an anchor). The source: assets/app/icon.svg (full-bleed square, as
drawn), rendered once to assets/app/icon-1024.png (rsvg-convert -w 1024 -h 1024 assets/app/icon.svg -o assets/app/icon-1024.png).

  gen_app_icon.py all           every icon below from icon-1024.png
  gen_app_icon.py OUT.icns      macOS: the square in macOS's grid (a margin, rounded corners), 16 .. 1024 px
  gen_app_icon.py OUT.png       the same at 1024 px (Linux, the Zero's launcher)

`all` writes assets/app/Kreisel.icns, assets/app/kreisel.png and the web app's icons (platforms/web: icon.svg, icon-180.png,
icon-192.png, icon-512.png: full-bleed, the phone rounds them itself).
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets/app/icon-1024.png"


def mac(size):
    """the drawing inside macOS's icon grid: 9 % margin, the corners rounded"""
    k = 4
    n = size * k
    m = round(n * 0.09)
    inner = n - 2 * m
    art = Image.open(SRC).convert("RGBA").resize((inner, inner), Image.LANCZOS)
    mask = Image.new("L", (inner, inner), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, inner - 1, inner - 1], radius=round(inner * 0.225), fill=255)
    im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    im.paste(art, (m, m), mask)
    return im.resize((size, size), Image.LANCZOS)


def icns(out):
    with tempfile.TemporaryDirectory() as t:
        iconset = Path(t) / "Kreisel.iconset"
        iconset.mkdir()
        for px in (16, 32, 128, 256, 512):
            mac(px).save(iconset / f"icon_{px}x{px}.png")
            mac(px * 2).save(iconset / f"icon_{px}x{px}@2x.png")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(out)], check=True)


def main(arg):
    if arg == "all":
        icns(ROOT / "assets/app/Kreisel.icns")
        mac(1024).save(ROOT / "assets/app/kreisel.png")
        art = Image.open(SRC).convert("RGB")
        for px in (180, 192, 512):
            art.resize((px, px), Image.LANCZOS).save(ROOT / f"platforms/web/icon-{px}.png", optimize=True)
        shutil.copyfile(ROOT / "assets/app/icon.svg", ROOT / "platforms/web/icon.svg")
        return
    out = Path(arg)
    if out.suffix == ".png":
        mac(1024).save(out)
    else:
        icns(out)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])

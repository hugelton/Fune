#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Bake a font's outlines into small C tables for the UI (a vector face: gfx.c fills them at any size).

  gen_vec_font.py OUT.h [--face NAME=FONT[@WGHT]:FIRST-LAST[+0xNN,...] ...]   (default: the Inter Tight faces S M L)

A face is the outlines of its characters as quadratic Bezier paths in font units, their advances and the kerning
pairs (units too), at one weight of a variable font. Nothing depends on a pixel size: the renderer scales.

Per face NAME:
  VF_<N>_OPS  one byte per path step: 0 move, 1 line, 2 quad, 3 close
  VF_<N>_PTS  int16 x, y pairs in that order: move 1 point, line 1, quad 2 (control, end)
  VF_<N>_G    vglyph_t {op, nop, pt, adv} per character: ASCII FIRST..LAST, then the extras (Latin-1 by code,
              the ellipsis U+2026 as code 0x85)
  VF_<N>_KERN / _KD  kerning pairs (key = a << 8 | b, sorted) and their deltas in units
  VF_<N>      vface_t
Digits share one advance (the widest) and are centred in it, so values do not jiggle.
"""
import argparse
import os
import sys
from pathlib import Path

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "assets" / "fonts" / "InterTight-VF.ttf"
EXTRAS = [0xA9, 0xB0, 0xB7, 0xC4, 0xD6, 0xDC, 0xE4, 0xF6, 0xFC, 0x2026, 0x2192]
CODE = {0x2026: 0x85, 0x2192: 0x86}   # (0x86: the arrow, "Duplicate \x86 2")
L_CHARS = " #+-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXY"      # what the big face draws (as the bitmap L)
KERN_MIN = 10                                              # units (about 0.5 % of the em)


class Path_(BasePen):
    def __init__(self, gs):
        super().__init__(gs)
        self.ops, self.pts = [], []

    def _moveTo(self, p): self.ops.append(0); self.pts += [round(p[0]), round(p[1])]
    def _lineTo(self, p): self.ops.append(1); self.pts += [round(p[0]), round(p[1])]
    def _qCurveToOne(self, c, p): self.ops.append(2); self.pts += [round(c[0]), round(c[1]), round(p[0]), round(p[1])]
    def _closePath(self): self.ops.append(3)
    def _endPath(self): self.ops.append(3)
    def _curveToOne(self, a, b, p):                        # (a cubic: two quadratics through the cubic's midpoint)
        raise SystemExit("cubic outlines are not supported")


def parse_face(s):
    name, _, rest = s.partition("=")
    spec, _, rng = rest.rpartition(":")
    font, _, wght = spec.partition("@")
    extras = []
    if "+" in rng:
        rng, ex = rng.split("+", 1)
        extras = [int(x, 0) for x in ex.split(",") if x]
    a, b = rng.split("-")
    return (name, font or str(FONT), float(wght) if wght else None, (int(a), int(b)), extras, None)


def presets():
    f = str(FONT)
    return [("S", f, 400.0, (32, 126), EXTRAS, None),
            ("M", f, 500.0, (32, 126), EXTRAS, None),
            ("L", f, 600.0, (32, 32), [ord(c) for c in L_CHARS[1:]], None)]


def kerning(path, wght, upm, chars):
    """pair deltas in units, measured with the shaper (GPOS) at one pixel per unit"""
    from PIL import ImageFont
    f = ImageFont.truetype(path, upm, layout_engine=ImageFont.Layout.RAQM)
    if wght:
        f.set_variation_by_axes([wght])
    one = {c: f.getlength(c) for c in chars if c != " "}
    out = {}
    for a in chars:
        for b in chars:
            if a == " " or b == " " or (a.isdigit() and b.isdigit()):
                continue
            d = round(f.getlength(a + b) - one[a] - one[b])
            if abs(d) >= KERN_MIN:
                out[(a, b)] = max(-32000, min(32000, d))
    return out


def emit(face):
    name, path, wght, (first, last), extras, _ = face
    font = TTFont(path)
    if wght and "fvar" in font:
        font = instancer.instantiateVariableFont(font, {"wght": wght})
    upm = font["head"].unitsPerEm
    gs, cmap, hmtx = font.getGlyphSet(), font.getBestCmap(), font["hmtx"]
    codes = list(range(first, last + 1)) + list(extras)
    chars = [chr(c) for c in codes]
    adv = {c: hmtx[cmap[ord(c)]][0] for c in chars}
    digits = [c for c in chars if c.isdigit()]
    wide = max((adv[c] for c in digits), default=0)
    ops, pts, table = [], [], []
    for c in chars:
        pen = Path_(gs)
        gs[cmap[ord(c)]].draw(pen)
        shift = 0
        if c.isdigit():
            shift = (wide - adv[c]) // 2
            adv[c] = wide
        p = pen.pts
        if shift:
            p = [v + shift if i % 2 == 0 else v for i, v in enumerate(p)]
        table.append((len(ops), len(pen.ops), len(pts) // 2, adv[c]))
        ops += pen.ops
        pts += p
    kern = kerning(path, wght, upm, [c for c in chars if ord(c) < 256 or c == "\u2026"])
    keys, deltas = [], []
    for (a, b), d in sorted(kern.items(), key=lambda kv: (ord(kv[0][0]) << 8) | ord(kv[0][1])):
        if ord(a) < 256 and ord(b) < 256:
            keys.append((ord(a) << 8) | ord(b))
            deltas.append(d)
    assert len(ops) < 65536 and len(pts) // 2 < 65536
    L = [f"/* {name}: {os.path.basename(path)}" + (f" @{wght:g}" if wght else "") + f", {len(chars)} outlines, "
         f"{len(ops)} steps, {len(pts) // 2} points */"]
    L.append(arr(f"VF_{name}_OPS", "uint8_t", ops, 32))
    L.append(arr(f"VF_{name}_PTS", "int16_t", pts, 16))
    L.append(f"static const vglyph_t VF_{name}_G[{len(table)}] = {{")
    L += [f"    {{{o}, {n}, {p}, {a}}}," for o, n, p, a in table]
    L.append("};")
    if keys:
        L.append(arr(f"VF_{name}_KERN", "uint16_t", keys, 12))
        L.append(arr(f"VF_{name}_KD", "int16_t", deltas, 16))
    ex = ", ".join(str(CODE.get(e, e)) for e in extras) or "0"
    L.append(f"static const uint8_t VF_{name}_EX[{max(1, len(extras))}] = {{{ex}}};")
    L.append(f"static const vface_t VF_{name} = {{{upm}, {first}, {last}, {len(extras)}, {len(keys)}, VF_{name}_G, "
             f"VF_{name}_OPS, VF_{name}_PTS, {'VF_%s_KERN' % name if keys else '0'}, "
             f"{'VF_%s_KD' % name if keys else '0'}, VF_{name}_EX}};")
    size = len(ops) + 2 * len(pts) + 8 * len(table) + 4 * len(keys)
    return "\n".join(L) + "\n", size, (len(chars), len(ops), len(pts) // 2, len(keys))


def arr(name, ctype, values, per):
    lines = [f"static const {ctype} {name}[{max(1, len(values))}] = {{"]
    for i in range(0, len(values), per):
        lines.append("    " + ", ".join(str(v) for v in values[i:i + per]) + ",")
    if not values:
        lines.append("    0,")
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--face", action="append", default=[])
    a = ap.parse_args()
    faces = [parse_face(s) for s in a.face] or presets()
    out = ["/* generated by tools/gen_vec_font.py: outlines (Inter Tight, SIL OFL 1.1, Copyright 2022 The Inter Project "
           "Authors; see assets/fonts/OFL-InterTight.txt) */", "#pragma once", "#include <stdint.h>", ""]
    total = 0
    for f in faces:
        text, size, (n, o, p, k) = emit(f)
        out.append(text)
        total += size
        print(f"vector face {f[0]}: {n} outlines, {o} steps, {p} points, {k} kerning pairs = {size} B")
    Path(a.out).write_text("\n".join(out))
    print(f"vector fonts total {total} B  -> {a.out}")


if __name__ == "__main__":
    sys.exit(main())

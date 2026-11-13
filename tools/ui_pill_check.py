#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Checks the pills under the labels of the row in hand (draw.c sk_pill) on every recorded screen.

    build/kreisel_sketch DIR && python3 -I tools/ui_pill_check.py DIR [PREFIX]

Each screen's shapes are the JSON lines kreisel_sketch writes (src/ui/gfx_rec.h). For each pill:
  - its label fits in it, with 2 px or more on each side, and fits the label's box;
  - it lies on the screen and inside the panel's frame around it;
  - it overlaps no other pill, no knob (its disc and arc: r + 2), no icon, and no other text but its own label.
Exits 1 when any pill fails, with the screen, the pill and what it hits."""
import json, os, sys

def boxes_overlap(a, b):
    return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]

def rect_circle(r, cx, cy, rad):
    nx = min(max(cx, r[0]), r[0] + r[2] - 1)
    ny = min(max(cy, r[1]), r[1] + r[3] - 1)
    return (nx - cx) ** 2 + (ny - cy) ** 2 < rad * rad

def check(path):
    w, h = (240, 240) if os.path.basename(path).startswith("fm1_") else (320, 170)
    recs = [json.loads(l) for l in open(path, encoding="latin-1") if l.strip()]   # (the firmware's one-byte codes: 0x85 ..., 0x86 the arrow)
    for n, r in enumerate(recs): r["n"] = n   # (the order drawn: what comes after a pill covers it, a dialog over the page)
    pills = [r for r in recs if r["k"] == "pill"]
    bad = []
    for n, p in enumerate(pills):
        frames = [r["i"][:4] for r in recs if r["k"] == "frame" and r["n"] < p["n"]]   # (the frames under it)
        x, y, pw, ph, tw, bw = p["i"]
        box, lab, why = (x, y, pw, ph), p.get("s", ""), []
        if any(r["k"] in ("frame", "panel", "drop") and r["n"] > p["n"] and boxes_overlap(box, r["i"][:4]) for r in recs):
            continue   # under a dialog drawn after it: not seen
        if tw + 4 > pw: why.append(f"label {tw}px in a {pw}px pill")
        if tw > bw: why.append(f"label {tw}px wider than its box {bw}px")
        if x < 0 or y < 0 or x + pw > w or y + ph > h: why.append("off the screen")
        inside = [f for f in frames if f[0] <= x and f[1] <= y and x + pw <= f[0] + f[2] and y + ph <= f[1] + f[3]]
        crossing = [f for f in frames if boxes_overlap(box, f) and f not in inside and not (f[0] >= x and f[1] >= y and f[0] + f[2] <= x + pw)]
        if crossing: why.append(f"across a frame's edge {crossing[0]}")
        for m, q in enumerate(pills):
            if m != n and boxes_overlap(box, q["i"][:4]): why.append(f"on the pill of {q.get('s')}")
        for r in recs:
            k, i = r["k"], r.get("i", [])
            if len(i) < 3: continue
            if k == "knob" and rect_circle(box, i[0], i[1], i[2] + 2): why.append(f"on a knob at {i[0]},{i[1]} r{i[2]}")
            elif k == "icon" and boxes_overlap(box, (i[0], i[1], i[2], i[2])): why.append(f"on an icon at {i[0]},{i[1]}")
            elif k == "textbox" and r.get("s") and r.get("s") != lab:
                # a box is wider than its words: the words' own extent (centred: ~5.5 px a letter of the small font)
                ww = int(len(r["s"]) * 5.5) + 2
                wx = i[0] + (i[2] - ww) // 2 if len(i) > 5 and i[5] == 0 else i[0]
                if boxes_overlap((x + 1, y + 1, pw - 2, ph - 2), (wx, i[1] + 2, ww, i[3] - 4)): why.append(f"on text '{r['s']}' {i[:4]}")
        if why: bad.append((lab, box, sorted(set(why))))
    return len(pills), bad

def main():
    d = sys.argv[1]
    prefix = sys.argv[2] if len(sys.argv) > 2 else "fm1_"
    total, fails = 0, 0
    for f in sorted(os.listdir(d)):
        if not f.startswith(prefix) or not f.endswith(".jsonl"): continue
        n, bad = check(os.path.join(d, f))
        total += n
        for lab, box, why in bad:
            fails += 1
            print(f"{f[:-6]}: '{lab}' {box}: " + "; ".join(why))
    print(f"{total} pills, {fails} failing")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()

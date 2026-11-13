#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Screens as Sketch shapes: the JSON lines kreisel_sketch writes (every g_* call of a frame, src/ui/gfx_rec.h) become
rectangles, ovals, paths and text in a .sketch file, one group a screen, in the renderer's own sizes and colours.

    KREISEL_SKETCH="organ_sel organ_edit drum_sel drum_edit" ./build/kreisel_sketch build/sketch
    python3 tools/ui_sketch.py TEMPLATE.sketch OUT.sketch build/sketch/*.jsonl

TEMPLATE is any .sketch saved by the Sketch in use (its document, meta and one layer of each kind are taken as the
templates, so the file is in that version's format). Rough on purpose: shadows, bevels and drops are left out, gradients
are flat (as the device draws them), icons are boxes. Nothing of TEMPLATE is changed."""
import copy, json, math, os, sys, uuid, zipfile

FONT = {0: ("Inter_Semi-Bold", 12, 15), 1: ("Inter_Semi-Bold", 15, 19), 2: ("Inter_Semi-Bold", 28, 35)}   # GF_S M L: size, line


def uid():
    return str(uuid.uuid4()).upper()


def load_template(path):
    z = zipfile.ZipFile(path)
    doc = json.loads(z.read("document.json"))
    meta = json.loads(z.read("meta.json"))
    pages = [json.loads(z.read(n)) for n in z.namelist() if n.startswith("pages/")]
    t, fill, border = {}, None, None

    def walk(l):
        nonlocal fill, border
        t.setdefault(l["_class"], l)
        st = l.get("style", {})
        if fill is None and st.get("fills"): fill = st["fills"][0]
        if border is None and st.get("borders"): border = st["borders"][0]
        for x in l.get("layers", []): walk(x)
    for p in pages:
        for l in p["layers"]: walk(l)
    for k in ("rectangle", "oval", "shapePath", "text", "group"):
        assert k in t, "the template has no %s layer" % k
    assert fill and border, "the template has no fill or no border to copy"
    return doc, meta, pages[0], t, fill, border


class Maker:
    def __init__(self, t, fill, border):
        self.t, self.fill, self.border = t, fill, border

    def color(self, c, a=1.0):
        return {"_class": "color", "alpha": round(a, 4), "red": ((c >> 16) & 255) / 255, "green": ((c >> 8) & 255) / 255,
                "blue": (c & 255) / 255}

    def base(self, kind, name, x, y, w, h):
        l = copy.deepcopy(self.t[kind])
        l["do_objectID"] = uid()
        l["name"] = name
        l["frame"] = {"_class": "rect", "constrainProportions": False, "x": float(x), "y": float(y),
                      "width": float(max(w, 0.5)), "height": float(max(h, 0.5))}
        l["rotation"] = 0
        l["isVisible"] = True
        l["isLocked"] = False
        st = l["style"]
        st["do_objectID"] = uid()
        st["fills"], st["borders"], st["shadows"] = [], [], []
        if "innerShadows" in st: st["innerShadows"] = []
        st["contextSettings"] = {"_class": "graphicsContextSettings", "blendMode": 0, "opacity": 1}
        return l

    def paint(self, l, fill=None, falpha=1.0, edge=None, thick=1.0, pos=1):
        st = l["style"]
        if fill is not None:
            f = copy.deepcopy(self.fill)
            f["isEnabled"], f["fillType"], f["color"] = True, 0, self.color(fill, falpha)
            st["fills"] = [f]
        if edge is not None:
            b = copy.deepcopy(self.border)
            b["isEnabled"], b["fillType"], b["color"], b["thickness"], b["position"] = True, 0, self.color(edge), float(thick), pos
            st["borders"] = [b]
        return l

    def rect(self, name, x, y, w, h, r, fill=None, falpha=1.0, edge=None, thick=1.0):
        l = self.base("rectangle", name, x, y, w, h)
        r = max(0, min(r, min(w, h) / 2))
        l["fixedRadius"] = float(r)
        l["points"] = [self.pt(px, py, r) for px, py in ((0, 0), (1, 0), (1, 1), (0, 1))]
        l["isClosed"] = True
        return self.paint(l, fill, falpha, edge, thick)

    def pt(self, x, y, r=0.0):
        return {"_class": "curvePoint", "cornerRadius": float(r), "cornerStyle": 0, "curveFrom": "{%g, %g}" % (x, y),
                "curveMode": 1, "curveTo": "{%g, %g}" % (x, y), "hasCurveFrom": False, "hasCurveTo": False,
                "point": "{%g, %g}" % (x, y)}

    def oval(self, name, cx, cy, r, fill=None, edge=None, thick=1.0):
        l = self.base("oval", name, cx - r, cy - r, 2 * r, 2 * r)
        k = 0.22385762508460333
        P = [((0.5, 1), (0.5 + k, 1), (0.5 - k, 1)), ((1, 0.5), (1, 0.5 - k), (1, 0.5 + k)),
             ((0.5, 0), (0.5 - k, 0), (0.5 + k, 0)), ((0, 0.5), (0, 0.5 + k), (0, 0.5 - k))]
        l["points"] = [{"_class": "curvePoint", "cornerRadius": 0, "cornerStyle": 0, "curveFrom": "{%g, %g}" % f,
                        "curveMode": 2, "curveTo": "{%g, %g}" % t, "hasCurveFrom": True, "hasCurveTo": True,
                        "point": "{%g, %g}" % p} for p, f, t in P]
        l["isClosed"] = True
        return self.paint(l, fill, 1.0, edge, thick, 1)

    def path(self, name, pts, closed=False, fill=None, edge=None, thick=1.0):
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        x0, y0 = min(xs), min(ys)
        w, h = max(max(xs) - x0, 0.5), max(max(ys) - y0, 0.5)
        l = self.base("shapePath", name, x0, y0, w, h)
        l["points"] = [self.pt((x - x0) / w, (y - y0) / h) for x, y in pts]
        l["isClosed"] = closed
        if "edited" in l: l["edited"] = True
        return self.paint(l, fill, 1.0, edge, thick, 0)

    def text(self, s, x, y, font, size, col, w=None, align=0):
        l = self.base("text", s, x, y, w or 10, 1)
        attrs = {"MSAttributedStringFontAttribute": {"_class": "fontDescriptor", "attributes": {"name": font, "size": size}},
                 "MSAttributedStringColorAttribute": self.color(col),
                 "paragraphStyle": {"_class": "paragraphStyle", "alignment": align}}
        l["attributedString"] = {"_class": "attributedString", "string": s,
                                 "attributes": [{"_class": "stringAttribute", "location": 0, "length": len(s), "attributes": attrs}]}
        l["style"]["textStyle"] = {"_class": "textStyle", "encodedAttributes": copy.deepcopy(attrs), "verticalAlignment": 0}
        l["textBehaviour"] = 1 if w else 0            # fixed width (a box) or as wide as the text
        l["frame"]["height"] = float(math.ceil(size * 1.25))
        if not w: l["frame"]["width"] = float(max(1, len(s)) * size * 0.62)
        return l

    def group(self, name, x, y, w, h, layers):
        g = copy.deepcopy(self.t["group"])
        g["do_objectID"], g["name"] = uid(), name
        g["frame"] = {"_class": "rect", "constrainProportions": False, "x": float(x), "y": float(y), "width": float(w), "height": float(h)}
        g["style"] = self.base("rectangle", "", 0, 0, 1, 1)["style"]
        g["layers"] = layers
        g["isVisible"], g["isLocked"] = True, False
        return g


def mix(a, b, pct):   # k_mix: a toward b by pct %
    return sum(((((a >> s) & 255) * (100 - pct) + ((b >> s) & 255) * pct) // 100) << s for s in (16, 8, 0))


def knob_layers(m, cx, cy, r, v, bip, arc, ring, mark):
    out = [m.oval("knob ring", cx, cy, r - 1, None, ring, 2)]
    a0 = math.radians(135)                     # down-left, then clockwise (screen y down) 270 degrees to down-right
    start = a0 + (math.radians(135) if bip else 0)
    end = a0 + math.radians(270) * max(0, min(100, v)) / 100
    if abs(end - start) > 0.01:
        n = max(2, int(abs(end - start) / 0.15) + 1)
        pts = [(cx + (r - 1) * math.cos(start + (end - start) * i / (n - 1)), cy + (r - 1) * math.sin(start + (end - start) * i / (n - 1))) for i in range(n)]
        out.append(m.path("knob arc", pts, False, None, arc, 2))
    out.append(m.path("knob mark", [(cx + (r - 6) * math.cos(end) * 0.35, cy + (r - 6) * math.sin(end) * 0.35),
                                    (cx + (r - 3) * math.cos(end), cy + (r - 3) * math.sin(end))], False, None, mark, 2))
    return out


def screen(m, path):
    L = []
    for line in open(path):
        e = json.loads(line)
        k, i, c, s = e["k"], e.get("i", []), e.get("c", []), e.get("s")
        if k in ("clear", "rect"):
            x, y, w, h = i[:4]
            r, a = (i[4], i[5]) if len(i) >= 6 else (0, 255)
            if a > 0: L.append(m.rect("rect", x, y, w, h, r, c[0] & 0xFFFFFF, a / 255))
        elif k == "frame":
            x, y, w, h, r = i
            L.append(m.rect("frame", x, y, w, h, r, c[0] & 0xFFFFFF, 1.0, c[1] & 0xFFFFFF, 1))
        elif k == "grad":
            x, y, w, h, r, a = i
            L.append(m.rect("gradient (flat)", x, y, w, h, r, mix(c[0] & 0xFFFFFF, c[1] & 0xFFFFFF, 50), a / 255))
        elif k == "around":
            x, y, w, h, hx, hy, hw, hh = i
            for rx, ry, rw, rh in ((x, y, w, hy - y), (x, hy + hh, w, y + h - hy - hh), (x, hy, hx - x, hh), (hx + hw, hy, x + w - hx - hw, hh)):
                if rw > 0 and rh > 0: L.append(m.rect("around", rx, ry, rw, rh, 0, c[0] & 0xFFFFFF))
        elif k in ("panel", "raised", "colored"):
            x, y, w, h, r = i[:5]
            L.append(m.rect(k, x, y, w, h, r, c[0] & 0xFFFFFF, 1.0, mix(c[0] & 0xFFFFFF, 0, 35), 1))
        elif k == "sunken":
            x, y, w, h, r = i
            L.append(m.rect("sunken", x, y, w, h, r, mix(c[0] & 0xFFFFFF, 0, 30)))
        elif k == "knobface":
            cx, cy, r = i
            L.append(m.oval("knob face", cx, cy, r, c[0] & 0xFFFFFF))
        elif k == "knob":
            cx, cy, r, v, bip = i
            L += knob_layers(m, cx, cy, r, v, bip, *(x & 0xFFFFFF for x in c))
        elif k == "line":
            x0, y0, x1, y1, t = i
            L.append(m.path("line", [(x0, y0), (x1, y1)], False, None, c[0] & 0xFFFFFF, max(1, t)))
        elif k == "curve":
            p = e["p"]
            pts = [(p[j], p[j + 1]) for j in range(0, len(p), 2)]
            if len(pts) < 2: continue
            if e["b"] >= 0:
                L.append(m.path("curve fill", pts + [(pts[-1][0], e["b"]), (pts[0][0], e["b"])], True, c[0] & 0xFFFFFF))
            else:
                L.append(m.path("curve", pts, False, None, c[0] & 0xFFFFFF, max(1, e["t"])))
        elif k == "icon":
            x, y, size, gid = i
            L.append(m.rect("icon %d" % gid, x, y, size, size, size // 4, (c[0] & 0xFFFFFF) or None, 1.0, c[1] & 0xFFFFFF, 1))
        elif k == "text" and s:
            x, y, f = i
            name, size, lh = FONT.get(f, FONT[0])
            L.append(m.text(s, x, y + (lh - size * 1.25) / 2, name, size, c[0] & 0xFFFFFF))
        elif k == "textbox" and s:
            x, y, w, h, f, al = i
            name, size, lh = FONT.get(f, FONT[0])
            L.append(m.text(s, x, y + (h - size * 1.25) / 2, name, size, c[0] & 0xFFFFFF, w, {-1: 0, 0: 2, 1: 1}[al]))
        elif k == "pix" and s:
            x, y, scale, kind = i
            L.append(m.text(s, x, y - scale, "Menlo-Bold", (8 if kind == 8 else 7) * scale + scale, c[0] & 0xFFFFFF))
        # shadow, drop, bevel: left out (rough)
    return L


def main(argv):
    if len(argv) < 4:
        raise SystemExit(__doc__)
    doc, meta, page_t, t, fill, border = load_template(argv[1])
    m = Maker(t, fill, border)
    groups, arts = [], {}
    for n, path in enumerate(argv[3:]):
        base = os.path.basename(path)[:-6]                        # fm1_organ_sel
        prof, scr = base.split("_", 1)
        w, h = (240, 240) if prof == "fm1" else (320, 180)
        eng, pg = scr.rsplit("_", 1)
        title = "%s - %s%s" % (eng.upper(), {"sel": "SOUND", "edit": "EDIT"}.get(pg, pg.upper()), "" if prof == "fm1" else " (320x180)")
        row = ["organ", "drum"].index(eng) if eng in ("organ", "drum") else 2
        col = {"sel": 0, "edit": 1}.get(pg, 2) + (0 if prof == "fm1" else 2)
        g = m.group(title, col * (340 if col < 2 else 380) + (40 if col >= 2 else 0), row * 300, w, h, screen(m, path))
        groups.append(g)
        arts[g["do_objectID"]] = {"name": title}
    page = copy.deepcopy(page_t)
    page["do_objectID"], page["name"], page["layers"] = uid(), "ORGAN DRUM (from the code)", groups
    doc = copy.deepcopy(doc)
    doc["do_objectID"] = uid()
    doc["pages"] = [{"_class": "MSJSONFileReference", "_ref_class": "MSImmutablePage", "_ref": "pages/" + page["do_objectID"]}]
    doc["currentPageIndex"] = 0
    for k in ("foreignSymbols", "foreignLayerStyles", "foreignTextStyles", "foreignSwatches", "fontReferences"):
        if k in doc: doc[k] = []
    meta = copy.deepcopy(meta)
    meta["pagesAndArtboards"] = {page["do_objectID"]: {"name": page["name"], "artboards": arts}}
    meta["autosaved"] = 0
    with zipfile.ZipFile(argv[2], "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("document.json", json.dumps(doc))
        z.writestr("meta.json", json.dumps(meta))
        z.writestr("user.json", json.dumps({}))
        z.writestr("pages/%s.json" % page["do_objectID"], json.dumps(page))
    print("%s: %d screens, %d layers" % (argv[2], len(groups), sum(len(g["layers"]) for g in groups)))


if __name__ == "__main__":
    main(sys.argv)

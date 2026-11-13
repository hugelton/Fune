#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Bake Fukiai icons (assets/icons/*.svg, MIT) into C tables for the UI, as Fukiai's Multicolor draws them.

  gen_icons.py OUT.h

An icon is up to two layers: the `background` (a face) and the `primary` (the line work). Fukiai's Multicolor shows
those two and hides the `secondary`; so do we. An SVG with no layers is all primary. The caller gives each layer
its own colour (g_icon2 in gfx.c).

Outlines are filled shapes (Fukiai stores expanded paths, not strokes). Curves become quadratics, in 1/4 units of the
500 x 500 viewBox (2000 per em), y up: the same tables as the text faces, so gfx.c fills them the same way.

  VI_OPS / VI_PTS   one byte per path step (0 move, 1 line, 2 quad, 3 close) / int16 x, y pairs
  VI_BG_G / VI_FG_G vglyph_t per icon (index: the GI_* enum), nop 0 for a layer an icon does not have
"""
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from fontTools.pens.basePen import BasePen
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path

ROOT = Path(__file__).resolve().parents[1]
ICONS = ROOT / "assets" / "icons"
NS = "{http://www.w3.org/2000/svg}"
SCALE = 4        # 500 units -> 2000
# the GI_* enum of gfx.h, in order, with the Fukiai file each takes
TABLE = [
    ("GI_PLAY", "play_fill"), ("GI_STOP", "stop_fill"),   # solid, one colour
    ("GI_PAGE_PATTERN", "grid_3x3"), ("GI_PAGE_SONG", "note_eighth"), ("GI_PAGE_SOUND", "wave_sine"),
    ("GI_PAGE_ENV", "adsr_linear"), ("GI_PAGE_FX", "fx"),
    ("GI_BATTERY_0", "battery_0"), ("GI_BATTERY_1", "battery_1"), ("GI_BATTERY_2", "battery_2"),
    ("GI_BATTERY_3", "battery_3"), ("GI_BATTERY_4", "battery_4"), ("GI_BATTERY_CHARGING", "battery_charging"),
    ("GI_RECORD", "record_fill"),
    ("GI_PAGE_MIXER", "slider_vertical"), ("GI_PAGE_BUS", "send"),
    # the six engines, in the order of src/engines_data.c (marked as Felucca 2.0 marks them)
    ("GI_ENG_ANALOG", "wave_variants"), ("GI_ENG_LOFI", "mask"), ("GI_ENG_NOISE", "wave_noise_white"), ("GI_ENG_PHASE", "phase"),
    ("GI_ENG_TRIO", "primitives"), ("GI_ENG_WHEEL", "slider_vertical"),
    ("GI_P_NOISE", "wave_noise_white"), ("GI_P_ENV", "adsr_linear"), ("GI_P_BITS", "mask"), ("GI_P_FOLD", "wave_fold"), ("GI_P_GENERIC", "knob"),
    # the parameters' icons (12 px, before a card's label); which one a label gets follows Felucca 2.0's table (icons.c)
    ("GI_P_ATTACK", "adsr_attack"), ("GI_P_DECAY", "adsr_decay"), ("GI_P_SUSTAIN", "adsr_sustain"), ("GI_P_RELEASE", "adsr_release"),
    ("GI_P_CUTOFF", "filter_lowpass"), ("GI_P_TONE", "filter_bandpass"), ("GI_P_DRIVE", "diode"), ("GI_P_LEVEL", "speaker_wave_2"),
    ("GI_P_WAVE", "wave_variants"), ("GI_P_TUNE", "tuning_fork"), ("GI_P_TIME", "clock"), ("GI_P_HOLD", "lock"),
    ("GI_P_CURVE", "curve_exp"), ("GI_P_AMOUNT", "amplitude"), ("GI_P_PITCH", "wave_pitch"), ("GI_P_SHAPE", "wave_variants_four"),
    ("GI_P_DIST", "signal_clip"), ("GI_P_CHORUS", "waves"), ("GI_P_ECHO", "echo"), ("GI_P_REVERB", "spring"),
    ("GI_P_PAN", "pan"), ("GI_P_SIZE", "arrow_scale"), ("GI_P_FEEDBACK", "feedback"), ("GI_P_RETURN", "signal_in"),
    ("GI_P_FADE", "wave_fade"), ("GI_P_OCTAVE", "arrow_up"), ("GI_P_GATE", "gate_unipolar"), ("GI_P_QUANT", "quantize"),
    ("GI_P_TRANSPOSE", "arrow_up_down"), ("GI_P_TEMPO", "metronome"), ("GI_P_SWING", "swing"),
    ("GI_PAGE_LFO", "wave_cosine"), ("GI_PAGE_ARP", "repeat"), ("GI_PAGE_SCL", "piano"), ("GI_PAGE_CLOCK", "clock"),
    ("GI_P_MIX", "combine"), ("GI_P_DETUNE", "phase_out"), ("GI_P_SUB", "bass"), ("GI_P_RESO", "filter_lowpass_peak"), ("GI_P_KEYTRACK", "keyboard"),
    ("GI_PAGE_DRUM", "drum"), ("GI_P_LEN", "arrow_to_end"), ("GI_P_VEL", "bolt_fill"),
    ("GI_SONG", "symbol_disc"),   # the song playing, in the header (from the Fukiai font's glyph)
    # the sequencer's tools (the always-shown column of a big screen); TRANSPOSE takes GI_P_TRANSPOSE
    ("GI_T_FILL", "grid_2x2"), ("GI_T_EUCLID", "circle"), ("GI_T_RANDOM", "dice"), ("GI_T_SHIFT", "arrow_right"),
    ("GI_T_HUMANIZE", "wave_noise_step"), ("GI_T_REVERSE", "play_reverse"), ("GI_T_COPY", "duplicate"), ("GI_T_PASTE", "document"),
    ("GI_T_CLEAR", "explosion"), ("GI_T_UNDO", "undo"),   # (CLEAR: Dynamite; the owner draws its own icon later)
    # the other pages' tools (SONG, SOUND, MIXER, FX) and the header's button that opens them on a touch screen
    ("GI_T_INSERT", "plus"), ("GI_T_DELETE", "minus"), ("GI_T_SWAP", "arrow_swap"), ("GI_T_INIT", "arrow_to_start"),
    ("GI_T_PAN", "pan"), ("GI_T_UNMUTE", "speaker_wave_2"), ("GI_T_TOOLS", "hammer_wrench"),   # (Fukiai 0.9: the owner's tools icon, 2026-11-13)
    ("GI_CONFIG", "gear"),   # the page switcher's way to the settings (a touch screen, a mouse)
    ("GI_CARET_L", "caret_left"), ("GI_CARET_R", "caret_right"),   # the desktop's engine box: its buttons
    ("GI_LIBRARY", "library"),   # the desktop's header: the presets (a click: their list)
    # the inserts' own (fx_icon): the ones the parameters' icons do not already give
    ("GI_FX_FILTER", "filter_lowpass"), ("GI_FX_WAH", "filter_bandpass"), ("GI_FX_PHASER", "phase"), ("GI_FX_TREMOLO", "wave_fade"),
    ("GI_FX_WOBBLE", "waves"), ("GI_FX_AMP", "horn"),
    ("GI_SAVE", "save"), ("GI_MENU", "menu"),   # the touch surface's column (Fukiai 0.9's save)
]


class Rec(BasePen):
    def __init__(self):
        super().__init__(None)
        self.ops, self.pts = [], []

    def _moveTo(self, p): self.ops.append(0); self.pts += [round(p[0]), round(p[1])]
    def _lineTo(self, p): self.ops.append(1); self.pts += [round(p[0]), round(p[1])]
    def _qCurveToOne(self, c, p): self.ops.append(2); self.pts += [round(c[0]), round(c[1]), round(p[0]), round(p[1])]
    def _closePath(self): self.ops.append(3)
    def _endPath(self): self.ops.append(3)
    def _curveToOne(self, a, b, p): raise SystemExit("cubic reached the recorder")


def layer_paths(root, ids):
    """the d= of every path in the first group named in ids, else (ids None) of every path outside a group"""
    out = []
    if ids is None:
        for el in root:
            if el.tag == NS + "path":
                out.append(el.get("d"))
        return out
    for g in root.iter(NS + "g"):
        if g.get("id") in ids:
            out += [p.get("d") for p in g.iter(NS + "path")]
            break
    return out


def bake(paths):
    rec = Rec()
    # svg (x, y down, 0..500) -> units (y up, 0..2000), then cubics to quadratics
    pen = TransformPen(Cu2QuPen(rec, max_err=2.0), (SCALE, 0, 0, -SCALE, 0, 500 * SCALE))
    for d in paths:
        parse_path(d, pen)
    return rec.ops, rec.pts


def main():
    out = Path(sys.argv[1])
    ops, pts, bg, fg = [], [], [], []
    for const, name in TABLE:
        root = ET.parse(ICONS / (name + ".svg")).getroot()
        groups = {g.get("id") for g in root.iter(NS + "g")}
        back = layer_paths(root, {"background"}) if "background" in groups else []
        if name.startswith("adsr_") and "secondary" in groups and "primary" in groups:   # a stage of the envelope: the whole shape dim behind, the stage lit
            back = layer_paths(root, {"secondary"})
        if "primary" in groups:
            front = layer_paths(root, {"primary"})
        elif "base" in groups:
            front = layer_paths(root, {"base"})
        else:
            front = layer_paths(root, None)
        for layer, paths in ((bg, back), (fg, front)):
            o, p = bake(paths)
            layer.append((len(ops), len(o), len(pts) // 2))
            ops += o
            pts += p
    n = len(TABLE)
    lines = ["/* generated by tools/gen_icons.py from assets/icons (Fukiai, MIT): do not edit */",
             "static const uint8_t VI_OPS[] = {" + ",".join(map(str, ops)) + "};",
             "static const int16_t VI_PTS[] = {" + ",".join(map(str, pts)) + "};"]
    for tag, layer in (("BG", bg), ("FG", fg)):
        lines.append("static const vglyph_t VI_%s_G[%d] = {" % (tag, n))
        lines += ["    [%s] = {%d, %d, %d, 0}," % (TABLE[i][0], o, c, p) for i, (o, c, p) in enumerate(layer)]
        lines.append("};")
    for tag in ("BG", "FG"):
        lines.append("static const vface_t VI_%s = {2000, 0, 0, 0, 0, VI_%s_G, VI_OPS, VI_PTS, 0, 0, 0};" % (tag, tag))
    out.write_text("\n".join(lines) + "\n")
    print("icons:", n, "steps:", len(ops), "points:", len(pts) // 2)


if __name__ == "__main__":
    main()

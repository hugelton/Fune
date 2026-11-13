#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Write src/engines_data.c: the parameter tables of Kreisel's engines (declared below).

  gen_engines.py [OUT.c]      (default: src/engines_data.c)

An engine there is a `static const engine_t X = { .name, .page_title = {a, b}, .edit = { {label, fmt, min, max, def,
names, unit} x 8 }, ... }` in eng_<x>.c, with its value names in `static const char *const N_* [] = {...}`. This reads
those (the names, the two page titles, the eight descriptors) and nothing else; the DSP is not touched. Run it again
when the engines change: the table is committed, so a build needs no Python for it.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"   # (ORDER is empty: no engine table is read from a file now)
ORDER = []             # engines read from an eng_<x>.c table (none: each is declared here)
UIDS = {"ANALOG": "KANL", "LOFI": "KLOF", "PHASE": "KPHS", "TRIO": "KTRI", "ORGAN": "KOGN", "ENSEMBLE": "KENS", "VECTOR": "KVEC", "FM2": "KFM2", "DRUM": "DRUM"}   # the engines' ids
# An engine names its macros, four of its eight parameters by label, in `.macro = {"A", "B", "C", "D"}` (what HOME's first row
# of cards turns for the track in hand). This table declares it.
MACROS = {"ANALOG": ["CUT", "RES", "DTN", "DRV"], "LOFI": ["CRSH", "DUTY", "VIB", "TONE"], "NOISE": ["COLR", "FREQ", "RES", "DENS"],
          "PHASE": ["DCW", "ENV", "DTN", "FOLD"], "TRIO": ["CUT", "RES", "DTN", "PW"], "WHEEL": ["BODY", "TOP", "PERC", "DRV"]}
FMT = ["F_INT", "F_PCT", "F_BIPCT", "F_TIME", "F_LFOHZ", "F_CUTOFF", "F_DB", "F_SEMI", "F_ENUM", "F_BPM", "F_NOTE",
       "F_ONOFF", "F_OCT", "F_STEPS"]


def names_of(text):
    """every `static const char *const N_X[] = {"a", "b"};` -> {N_X: [a, b]}"""
    out = {}
    for m in re.finditer(r"static\s+const\s+char\s+\*const\s+(N_\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};", text, re.S):
        out[m.group(1)] = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2))
    return out


def engine_of(text, names):
    name = re.search(r'\.name\s*=\s*"(\w+)"', text).group(1)
    pages = re.search(r'\.page_title\s*=\s*\{\s*"(\w+)"\s*,\s*"(\w+)"\s*\}', text).groups()
    body = text[text.index(".edit = {"):]
    body = body[body.index("{") + 1:]
    descs, depth, cur, i = [], 0, "", 0
    # the eight `{...}` entries of the initializer
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                cur = ""
                continue
        elif ch == "}":
            depth -= 1
            if depth == 0:
                descs.append(cur)
                cur = ""
                if len(descs) == 8:
                    break
                continue
            if depth < 0:
                break
        if depth >= 1:
            cur += ch
    assert len(descs) == 8, (name, len(descs))
    out = []
    for d in descs:
        d = re.sub(r"/\*.*?\*/", "", d, flags=re.S)
        f = [x.strip() for x in d.split(",")]
        label, fmt, lo, hi, de, nm, unit = f[:7]
        assert fmt in FMT, (name, fmt)
        out.append(dict(label=label.strip('"'), fmt=FMT.index(fmt), min=int(lo), max=int(hi), default=int(de),
                        names=names.get(nm) if nm not in ("0", "") else None, nmref=nm if nm not in ("0", "") else None,
                        unit=unit.strip('"') if unit != "0" else None))
    return name, pages, out


def macro_of(text, name):
    """the engine's `.macro = {"A", "B", "C", "D"}`, else MACROS[name]"""
    m = re.search(r"\.macro\s*=\s*\{(.*?)\}", text, re.S)
    return re.findall(r'"(\w+)"', m.group(1)) if m else MACROS[name]


def macro_index(name, descs, labels):
    labels = list(labels)
    assert len(labels) == 4, (name, labels)
    idx = [[d["label"] for d in descs].index(l) for l in labels]   # (a label that is not one of its eight: an error here)
    return idx


def P(label, fmt, lo, hi, de, names=None, nmref=None, unit=None):
    return dict(label=label, fmt=fmt, min=lo, max=hi, default=de, nmref=nmref, names=names, unit=unit)


# Kreisel's own ANALOG, first in the table: two oscillators each with its wave, OSC 2's
# pitch (semitones), the detune and the levels, noise, sync, the filter. Twelve parameters, in the order SYNTH turns them
# SOUND (K1 on, page2 12): PITCH GLIDE DTN SYNC | WAVE1 LVL1 WAVE2 LVL2 | SUB SUBP NOIS COLOR, then EDIT (VCF, VCA)
# CUT RES DRV KTR | TYPE ENV LEVEL VEL; ENV is the host's ENV 1 -> filter amount (KF_LINK_FENV), not the engine's.
# (2026-11-06: no PW any more; the pulse widths are waves: P12 P25 P37, SQR the 50 %.)
# Its DSP: src/eng_kanalog.c
AN_WAVES = ["SAW", "SQR", "TRI", "SIN", "P12", "P25", "P37"]   # (0 .. 3 as before; 4 was PWM)
PD_WAVES = ["SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"]
TRIO_WAVES = ["TRI", "SAW", "PLS", "NOI", "S&T", "P&S"]
OWN_FIRST = [dict(name="ANALOG", pages=("PITCH", "OSC", "SUB"), drum=False, macro=["CUT", "RES", "DTN", "DRV"], lanes=None, page2=12,
                  edit=[P("PITCH", 7, -24, 24, 0), P("GLIDE", 3, 0, 127, 0), P("DTN", 0, 0, 127, 8, unit="ct"), P("SYNC", 11, 0, 1, 0),
                        P("WAVE1", 8, 0, 6, 0, AN_WAVES, "N_WAVE"), P("LVL1", 1, 0, 127, 110),
                        P("WAVE2", 8, 0, 6, 0, AN_WAVES, "N_WAVE"), P("LVL2", 1, 0, 127, 90),
                        P("SUB", 1, 0, 127, 0), P("SUBP", 8, 0, 2, 1, ["-2", "-1", "+1"], "N_SUBP"), P("NOIS", 1, 0, 127, 0),
                        P("COLOR", 1, 0, 127, 0),
                        P("CUT", 5, 0, 127, 90), P("RES", 1, 0, 127, 30), P("DRV", 1, 0, 127, 0), P("KTR", 1, 0, 127, 64),
                        P("TYPE", 8, 0, 2, 0, ["LP", "BP", "HP"], "N_FTYPE"), P("ENV", 15, 0, 100, 50),
                        P("LEVEL", 1, 0, 127, 127), P("VEL", 1, 0, 127, 64)]),
              # Kreisel's LOFI (src/eng_klofi.c): a console's voice, NOISE folded in. SOUND: PULSE, MOTION, NOISE; EDIT the rest.
              # (2026-11-06) SOUND a row each as the knobs take them: PULSE WAVE DUTY CRSH SRATE | MOTION SWP VIB ARP RATE |
              # NOISE NOISE NLVL COLOR TONE; EDIT STEP LEVEL VEL.
              dict(name="LOFI", pages=("PULSE", "MOTION", "NOISE"), drum=False, macro=["DUTY", "CRSH", "TONE", "NLVL"], lanes=None, page2=12,
                   edit=[P("WAVE", 8, 0, 3, 0, ["PLS", "TRI", "SAW", "OFF"], "N_LWAVE"), P("DUTY", 8, 0, 3, 2, ["12%", "25%", "50%", "75%"], "N_DUTY"),
                         P("CRSH", 8, 0, 7, 0, ["16BIT", "14BIT", "12BIT", "10BIT", "8BIT", "6BIT", "4BIT", "1BIT"], "N_CRSH"),
                         P("SRATE", 1, 0, 127, 0),
                         P("SWP", 0, -64, 63, 0), P("VIB", 0, 0, 15, 0),
                         P("ARP", 8, 0, 4, 0, ["OFF", "OCT", "MAJ", "MIN", "5TH"], "N_ARP"), P("RATE", 0, 1, 8, 2, unit="F"),
                         P("NOISE", 8, 0, 4, 0, ["OFF", "WHITE", "LONG", "SHORT", "DUST"], "N_NOISE"), P("NLVL", 1, 0, 127, 80),
                         P("COLOR", 1, 0, 127, 80), P("TONE", 1, 0, 127, 127),
                         P("STEP", 11, 0, 1, 1), P("LEVEL", 1, 0, 127, 127), P("VEL", 1, 0, 127, 64)]),
              # Kreisel's ORGAN (src/eng_korgan.c): the drawbar organ. SOUND: PERC ROTR VIB | the nine
              # bars (16' .. 1', four a row); EDIT: PVOL PDCY CLICK DRV | LEVEL VEL.
              dict(name="ORGAN", pages=("", "BARS", "BARS"), drum=False, macro=["8'", "4'", "PERC", "ROTR"], lanes=None, page2=12,
                   edit=[P("ROTR", 8, 0, 2, 1, ["OFF", "SLOW", "FAST"], "N_OROT"),
                         P("VIB", 8, 0, 6, 0, ["OFF", "V1", "V2", "V3", "C1", "C2", "C3"], "N_OVIB"),
                         P("PERC", 8, 0, 2, 2, ["OFF", "2ND", "3RD"], "N_OPERC"),
                         P("16'", 0, 0, 8, 8), P("5.3'", 0, 0, 8, 8), P("8'", 0, 0, 8, 8), P("4'", 0, 0, 8, 0), P("2.7'", 0, 0, 8, 0),
                         P("2'", 0, 0, 8, 0), P("1.6'", 0, 0, 8, 0), P("1.3'", 0, 0, 8, 0), P("1'", 0, 0, 8, 0),
                         P("PVOL", 8, 0, 1, 0, ["NORM", "SOFT"], "N_OPVOL"), P("PDCY", 8, 0, 1, 0, ["FAST", "SLOW"], "N_OPDCY"),
                         P("CLICK", 1, 0, 127, 10), P("DRV", 1, 0, 127, 0), P("LEVEL", 1, 0, 127, 127), P("VEL", 1, 0, 127, 0)]),
              # Kreisel's PHASE (src/eng_kphase.c): two lines of phase distortion. SOUND: PITCH WAVE1 WAVE2 | DCW1 DCW2 DTN
              # LINE | LVL1 LVL2 SUB FOLD; EDIT (DCW, DCA): DCW KTR DRV NOIS | ENV LEVEL VEL (ENV: the host's ENV 1 -> DCW).
              # PHASE (2026-11-06): a row a line, in the classic phase-distortion order: its wave, then how far each of the three
              # envelopes (DCO pitch, DCW wave, DCA level) take it. SOUND: PITCH GLIDE DTN LINE | WAVE1 DCO1 DCW1 DCA1 |
              # WAVE2 DCO2 DCW2 DCA2; EDIT: SUB FOLD NOIS DRV | KTR LEVEL VEL. The envelopes: one page, DCO DCW DCA.
              dict(name="PHASE", pages=("PITCH", "LINE 1", "LINE 2"), drum=False, macro=["DCW1", "DCW2", "DTN", "FOLD"], lanes=None, page2=12,
                   edit=[P("PITCH", 7, -24, 24, 0), P("GLIDE", 3, 0, 127, 0), P("DTN", 0, 0, 50, 6, unit="ct"),
                         P("LINE", 8, 0, 1, 0, ["MIX", "RING"], "N_PDL"),
                         P("WAVE1", 8, 0, 7, 0, PD_WAVES, "N_PDW"), P("DCO1", 7, -24, 24, 0), P("DCW1", 1, 0, 127, 80), P("DCA1", 1, 0, 127, 127),
                         P("WAVE2", 8, 0, 7, 1, PD_WAVES, "N_PDW"), P("DCO2", 7, -24, 24, 0), P("DCW2", 1, 0, 127, 60), P("DCA2", 1, 0, 127, 80),
                         P("SUB", 1, 0, 127, 0), P("FOLD", 1, 0, 127, 0), P("NOIS", 1, 0, 127, 0), P("DRV", 1, 0, 127, 0),
                         P("KTR", 1, 0, 127, 32), P("LEVEL", 1, 0, 127, 127), P("VEL", 1, 0, 127, 64)]),
              # Kreisel's TRIO (src/eng_ktrio.c): three oscillators, chip style. SOUND: W1 W2 W3 | INT2 INT3 DTN PW | LVL1 LVL2 LVL3
              # MOD; EDIT (VCF, VCA): MODE CUT RES KTR | DRV ENV LEVEL VEL.
              # (2026-11-06) SOUND: INT2 INT3 DTN MOD | W1 W2 W3 PW | LVL1 LVL2 LVL3: a column an oscillator in the last two rows.
              dict(name="TRIO", pages=("PITCH", "WAVES", "LEVELS"), drum=False, macro=["CUT", "RES", "PW", "INT2"], lanes=None, page2=11,
                   edit=[P("INT2", 7, -24, 24, 7), P("INT3", 7, -24, 24, -12), P("DTN", 0, 0, 50, 6, unit="ct"),
                         P("MOD", 8, 0, 3, 0, ["OFF", "RING", "SYNC", "BOTH"], "N_TMOD"),
                         P("W1", 8, 0, 5, 1, TRIO_WAVES, "N_TW"), P("W2", 8, 0, 5, 0, TRIO_WAVES, "N_TW"), P("W3", 8, 0, 5, 2, TRIO_WAVES, "N_TW"),
                         P("PW", 1, 0, 127, 64),
                         P("LVL1", 1, 0, 127, 127), P("LVL2", 1, 0, 127, 90), P("LVL3", 1, 0, 127, 50),
                         P("MODE", 8, 0, 3, 0, ["LP", "BP", "HP", "NOTCH"], "N_TMODE"), P("CUT", 5, 0, 127, 80), P("RES", 1, 0, 127, 40),
                         P("KTR", 1, 0, 127, 64), P("DRV", 1, 0, 127, 0), P("ENV", 15, 0, 100, 50), P("LEVEL", 1, 0, 127, 127), P("VEL", 1, 0, 127, 64)])]

# Divide-down strings: five registrations, shared three-tap ensemble, and a tone section.
# Sound parameters 0..10, EDIT parameters 11..15 (ENV links to ENV 1 -> FLT).
OWN_FIRST.append(dict(name="ENSEMBLE", pages=("", "STRINGS", "ENSEMBLE"), drum=False,
                      macro=["VLN", "TONE", "ENS", "DEP"], lanes=None, page2=11,
                      edit=[P("BASS", 1, 0, 127, 38), P("CELLO", 1, 0, 127, 70),
                            P("VIOLA", 1, 0, 127, 95), P("VLN", 1, 0, 127, 80),
                            P("BRASS", 1, 0, 127, 0), P("TONE", 5, 0, 127, 90),
                            P("ENS", 1, 0, 127, 100), P("RATE", 1, 0, 127, 48),
                            P("DEP", 1, 0, 127, 75), P("DTN", 0, 0, 50, 5, unit="ct"),
                            P("LEVEL", 1, 0, 127, 100),
                            P("HPF", 1, 0, 127, 5), P("DRV", 1, 0, 127, 0),
                            P("NOIS", 1, 0, 127, 0), P("ENV", 15, 0, 100, 50),
                            P("VEL", 1, 0, 127, 0)]))
WITH_ENSEMBLE = False   # ENSEMBLE is out of the MVP (2026-11-07, 0.0.1 Beta); True builds it in (with src/kreisel_engine.h K_WITH_ENSEMBLE 1)
OWN_FIRST = [next(o for o in OWN_FIRST if o["name"] == n) for n in ("ANALOG", "PHASE", "TRIO", "LOFI", "ORGAN") + (("ENSEMBLE",) if WITH_ENSEMBLE else ())]   # the order SOUND steps them (the owner's)

# New lightweight 2D vector wavetable synth: the first six parameters on
# SOUND, four corner-wave selectors on EDIT. Insert before DRUM to preserve its
# last-index property and all older engine IDs.
WITH_VECTOR = False   # VECTOR is not in the MVP (2026-11-06); True builds it in (with src/kreisel_engine.h K_WITH_VECTOR 1)
if WITH_VECTOR: OWN_FIRST.append(dict(name="VECTOR", pages=("", "MORPH", "WAVES"), drum=False,
                      macro=["X", "Y", "MOVE", "SPRD"], lanes=None, page2=6,
                      edit=[P("X", 1, 0, 127, 64), P("Y", 1, 0, 127, 64),
                            P("MOVE", 1, 0, 127, 0), P("SPRD", 1, 0, 127, 22),
                            P("RATE", 1, 0, 127, 32), P("LEVEL", 1, 0, 127, 103),
                            P("A", 8, 0, 7, 0, ["SINE", "SAW", "PULSE", "TRI", "VOX", "GLASS", "STRING", "BELL"], "N_VWT"),
                            P("B", 8, 0, 7, 1, ["SINE", "SAW", "PULSE", "TRI", "VOX", "GLASS", "STRING", "BELL"], "N_VWT"),
                            P("C", 8, 0, 7, 4, ["SINE", "SAW", "PULSE", "TRI", "VOX", "GLASS", "STRING", "BELL"], "N_VWT"),
                            P("D", 8, 0, 7, 5, ["SINE", "SAW", "PULSE", "TRI", "VOX", "GLASS", "STRING", "BELL"], "N_VWT")]))


DRUM_PIECES = ["T8 BD", "T9 BD", "K5 BD", "T8 SD", "T9 SD", "K7 SD", "T8 TOM", "T9 TOM", "K7 TOM", "T8 CNG", "T8 RS", "T9 RS", "K5 RS",
               "T8 CL", "K5 CL", "T8 CP", "K7 CP", "T8 CB", "K5 CB", "T8 MA", "V1 SHA", "T8 CH", "T8 OH", "K5 CH", "K5 OH", "K7 CH",
               "K7 OH", "K5 CY", "K7 CY", "V1 PO", "V1 PI",
               "SP BD", "SP CH", "SP OH", "TP ZP"]   # (src/drum_kreisel.c DP[], in its order; tests/audio_test.c checks; SP: the samples, assets/drums)
# Kreisel's DRUM (src/drum_kreisel.c): eight lanes, each a piece after the owner's JRMS models (T8 T9 K5 K7 V1). Its first ten are
# the lane in hand's (the core keeps them per lane: KApp.lane_p; kreisel.c DRUM_LANE says which), all on SOUND, in its rows: PIECE LVL PAN |
# TUNE TONE DCAY CLIK | ATK PDEP PTIM. The rest are the kit's, on EDIT: KTUN KDCY CRSH HUMN. No ENV or LFO page (k_page_unused).
OWN = [dict(name="DRUM", pages=("LANE", "KIT"), drum=True, macro=["TUNE", "TONE", "DCAY", "LVL"],
            lanes=["BD", "SD", "CH", "OH", "CP", "LT", "HT", "RS"],
            page2=10,   # SOUND: the lane in hand (its ten, KApp.lane_p, in the order the rows show them); EDIT: the kit's four
            edit=[dict(label="PIECE", fmt=8, min=0, max=len(DRUM_PIECES) - 1, default=0, nmref="N_PIECE", names=DRUM_PIECES, unit=None),
                  dict(label="LVL", fmt=1, min=0, max=127, default=100, nmref=None, names=None, unit=None),
                  dict(label="PAN", fmt=16, min=0, max=100, default=50, nmref=None, names=None, unit=None),
                  dict(label="TUNE", fmt=16, min=0, max=127, default=64, nmref=None, names=None, unit=None),
                  dict(label="TONE", fmt=16, min=0, max=127, default=64, nmref=None, names=None, unit=None),
                  dict(label="DCAY", fmt=16, min=0, max=127, default=64, nmref=None, names=None, unit=None),
                  dict(label="CLIK", fmt=0, min=0, max=100, default=0, nmref=None, names=None, unit=None),
                  dict(label="ATK", fmt=0, min=0, max=100, default=0, nmref=None, names=None, unit=None),
                  dict(label="PDEP", fmt=16, min=0, max=100, default=50, nmref=None, names=None, unit=None),
                  dict(label="PTIM", fmt=16, min=0, max=100, default=50, nmref=None, names=None, unit=None),
                  dict(label="KTUN", fmt=16, min=0, max=127, default=64, nmref=None, names=None, unit=None),
                  dict(label="KDCY", fmt=16, min=0, max=127, default=64, nmref=None, names=None, unit=None),
                  dict(label="CRSH", fmt=1, min=0, max=127, default=0, nmref=None, names=None, unit=None),
                  dict(label="HUMN", fmt=1, min=0, max=127, default=0, nmref=None, names=None, unit=None)])]

# FM2 (src/eng_kfm2.c): two operators, OP2 into OP1. Before DRUM, as the others (DRUM stays last; songs name engines by id).
# (2026-11-06) SOUND: PITCH TUNE RATIO GLIDE (the pitch) | INDEX ENV FEED KTR (how far OP2 moves OP1); EDIT: MIX DRIVE LEVEL VEL.
OWN_FIRST.append(dict(name="FM2", pages=("PITCH", "INDEX", "OUTPUT"), drum=False, macro=["INDEX", "RATIO", "FEED", "ENV"], lanes=None, page2=8,
                 edit=[P("PITCH", 7, -24, 24, 0), P("TUNE", 0, -50, 50, 0, unit="ct"),
                       P("RATIO", 8, 0, 7, 1, ["1/2", "1", "2", "3", "4", "6", "8", "16"], "N_RATIO"),
                       P("GLIDE", 3, 0, 127, 0),
                       P("INDEX", 1, 0, 127, 50), P("ENV", 1, 0, 127, 80), P("FEED", 1, 0, 127, 0), P("KTR", 1, 0, 127, 0),
                       P("MIX", 1, 0, 127, 0), P("DRIVE", 1, 0, 127, 0), P("LEVEL", 1, 0, 127, 120), P("VEL", 1, 0, 127, 64)]))
OWN_LAST = []

# What each engine's DSP leaves of the host's modulation (K_NOMOD_FILTER 1, K_NOMOD_SHAPE 2: ENV 1's and the LFO's FLT, SHP are shown
# dim), and whether it starts with a gate envelope (an organ). Read from its DSP: ANALOG and TRIO read the cutoff and the shape,
# PHASE the cutoff (as its DCW), LOFI and ORGAN neither.
NOMOD = {"LOFI": (3, 0), "ORGAN": (3, 1), "PHASE": (2, 0), "VECTOR": (3, 0), "FM2": (2, 0)}   # (DRUM reads the LFO's FLT and SHP as its lanes' TONE and DCAY)


# the names shown, by id (src/kreisel_engine_names.h: one place for the C and these tables); an engine's key here stays its old name
NAMES = dict(re.findall(r'#define K_ENGINE_NAME_(\w{4}) "([^"]*)"', (ROOT / "src" / "kreisel_engine_names.h").read_text()))
def shown(name):
    return NAMES.get(UIDS.get(name, ""), name)


def c(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "src" / "engines_data.c"
    lines = ["/* generated by tools/gen_engines.py: the engines' parameter tables. Do not edit. */",
             '#include "kreisel_engine.h"', "#if K_WITH_VECTOR != %d" % (1 if WITH_VECTOR else 0),
             '#error "K_WITH_VECTOR and tools/gen_engines.py WITH_VECTOR disagree: run the generator again"', "#endif",
             "#if K_WITH_ENSEMBLE != %d" % (1 if WITH_ENSEMBLE else 0),
             '#error "K_WITH_ENSEMBLE and tools/gen_engines.py WITH_ENSEMBLE disagree: run the generator again"', "#endif", ""]
    engines, used = [], {}
    for e in ORDER:
        text = (SRC / ("eng_%s.c" % e)).read_text()
        names = names_of(text)
        name, pages, descs = engine_of(text, names)
        for d in descs:
            if d["nmref"]:
                assert d["names"], (name, d["nmref"])
                used["%s_%s" % (name, d["nmref"])] = d["names"]
                d["sym"] = "KN_%s_%s" % (name, d["nmref"])
        engines.append((name, pages, descs, macro_index(name, descs, macro_of(text, name))))
    for o in OWN_FIRST + OWN + OWN_LAST:
        for d in o["edit"]:
            if d["nmref"]:
                d["sym"] = "KN_%s_%s" % (o["name"], d["nmref"])
                used[d["sym"][3:]] = d["names"]
    for sym, vals in used.items():
        lines.append("static const char *const KN_%s[] = {%s, 0};" % (sym, ", ".join(c(v) for v in vals)))
    lines += ["", "const KEngine K_ENGINE[K_ENGINE_COUNT] = {"]
    engines = [(n, p + ("",), d, 0, None, m, 4) for n, p, d, m in engines]   # (eight: four on SOUND, four on EDIT, as their two pages)
    engines = [(o["name"], o["pages"], o["edit"], 0, None, macro_index(o["name"], o["edit"], o["macro"]), o["page2"]) for o in OWN_FIRST] + engines
    engines += [(o["name"], tuple(o["pages"]) + ("",), o["edit"], 1, o["lanes"], macro_index(o["name"], o["edit"], o["macro"]), o.get("page2", 5)) for o in OWN]
    engines += [(o["name"], o["pages"], o["edit"], 0, None, macro_index(o["name"], o["edit"], o["macro"]), o["page2"]) for o in OWN_LAST]
    for name, pages, descs, drum, lanes, macro, page2 in engines:
        u = UIDS[name]
        lines.append("    {%s, K_UID('%s', '%s', '%s', '%s'), {%s}, {" % (c(shown(name)), u[0], u[1], u[2], u[3], ", ".join(c(x) for x in pages)))
        for d in descs:
            lines.append("        {%s, %d, %d, %d, %d, %s, %s}," % (c(d["label"]), d["fmt"], d["min"], d["max"], d["default"],
                                                                 d.get("sym") or "0", c(d["unit"]) if d["unit"] else "0"))
        nomod, gate = NOMOD.get(name, (0, 0))
        lines.append("    }, %d, {%s}, {%s}, %d, %d, %d, %d}," % (drum, ", ".join(c(l) for l in lanes) if lanes else "0", ", ".join(str(i) for i in macro), len(descs), page2, nomod, gate))
    lines.append("};")
    out.write_text("\n".join(lines) + "\n")
    print("engines:", len(engines), "->", out)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""MURIYA: the owner's MIDI sketch (examples/midi/Muriya.mid, six parts) arranged for Fune's four tracks, written as a score
for tools/make_demo_song.py (examples/muriya.json). A bar is a scene; each track keeps at most four one-bar patterns.

  T1 Retro   a synth bass: the bass's eighths an octave down; from bar 72 the off-beats an octave up
  T2 Phase   the pad (patterns 1, 2), and with the strings' low note under it where the strings play (3, 4)
  T3 Digital piano: its four-bar phrase (the last bar's variant folded into the fourth)
  T4 Trio    lead: its notes cut to the bar, a held note struck again on the next bar; before it, the drone
"""
import json
import sys
from pathlib import Path

import mido

SRC = Path(sys.argv[1] if len(sys.argv) > 1 else "examples/midi/Muriya.mid")
OUT = Path(sys.argv[2] if len(sys.argv) > 2 else "examples/muriya.json")
OFF = 4   # a scene's cell: the track silent

mid = mido.MidiFile(SRC)
TPB = mid.ticks_per_beat
STEP, BAR = TPB // 4, TPB * 4
tempo = next(m.tempo for m in mid.tracks[0] if m.type == "set_tempo")
BPM = round(60_000_000 / tempo)


def parts():
    out = {}
    for t in mid.tracks[1:]:
        now, on, notes = 0, {}, []
        for m in t:
            now += m.time
            if m.type == "note_on" and m.velocity:
                on[m.note] = (now, m.velocity)
            elif m.type in ("note_on", "note_off") and m.note in on:
                s, v = on.pop(m.note)
                notes.append((s, now - s, m.note))
        bars = {}
        for s, d, n in notes:
            bars.setdefault(s // BAR, []).append((round((s % BAR) / STEP), max(1, round(d / STEP)), n))
        out[t.name] = {b: sorted(v) for b, v in bars.items()}
    return out


P = parts()
first = min(min(b) for b in P.values() if b)
last = max(max(b) for b in P.values() if b)
BARS = range(first, last + 1)


def ev(step0, length, note, vel):   # make_demo_song's event [step from 1, note, length cut to the bar, velocity]
    return [step0 + 1, note, min(length, 16 - step0), vel]


def chord_of(bar):   # 0: the A bar (bass A, pad with A, strings on A), 1: the G/C bar
    for part, mark in (("bass", 57), ("string", 57), ("pad", 69)):
        if bar in P[part]:
            return 0 if any(n == mark for _, _, n in P[part][bar]) else 1
    return OFF


low = {0: 57, 1: 55}
pad = {0: [60, 69, 72], 1: [60, 67, 72]}
pad_low = {0: [57, 64, 72], 1: [55, 64, 72]}   # (three notes: a track has six voices, and a bar's chord still sounds as the next comes)
strings = {0: [57, 60, 64], 1: [55, 60, 64]}

# T1 Retro, a synth bass: the bass's eighths an octave down (patterns 1, 2); from bar 72 the off-beats jump an octave (3, 4)
src = [next(b for b in BARS if b in P["bass"] and chord_of(b) == c) for c in (0, 1)]
t1_pat = [[ev(s, 1, n - 12, 100 if s % 4 == 0 else 80) for s, _, n in P["bass"][b]] for b in src] + \
         [[ev(s, 1, n - 12 if s % 4 == 0 else n, 104 if s % 4 == 0 else 86) for s, _, n in P["bass"][b]] for b in src]
t1 = [chord_of(b) + (2 if b >= 72 else 0) if b in P["bass"] else OFF for b in BARS]

# T2 Phase: the pad (patterns 1, 2); the pad with the strings' low note under it wherever the strings play (3, 4)
pad_pat = [[ev(0, 16, n, 70) for n in pad[c]] for c in (0, 1)] + [[ev(0, 16, n, 72) for n in pad_low[c]] for c in (0, 1)]
t2 = [chord_of(b) + (2 if b in P["string"] else 0) if b in P["pad"] or b in P["string"] else OFF for b in BARS]

# T3: piano, its phrase ABCD
seen = []
for b in BARS:
    if b in P["piano"] and P["piano"][b] not in seen:
        seen.append(P["piano"][b])
piano_pat = [[ev(s, d, n, 88 if s % 4 == 0 else 74) for s, d, n in bar] for bar in seen[:4]]
t3 = []
for b in BARS:
    if b not in P["piano"]:
        t3.append(OFF)
        continue
    i = seen.index(P["piano"][b])
    t3.append(min(i, 3))   # (the fifth, the fourth bar's variant: the fourth)

# T4: lead, the phrase A . B C D . E .
lead_pat = [[ev(0, 16, 67, 92)],                          # G held (A and D, and their second bars)
            [ev(0, 6, 64, 92), ev(6, 10, 62, 88)],        # B
            [ev(0, 16, 64, 90)]]                          # C, and E's second bar (E itself: B, nearly the same)
t4 = []
lead_pat.append([ev(0, 16, 60, 60)])   # the drone's C, before the lead comes in (the bass's track is a synth bass now)
first_lead = min(P["lead"])
for b in BARS:
    if b < first_lead:
        t4.append(3 if b in P["drone"] else OFF)
        continue
    notes = P["lead"].get(b)
    if notes:
        if notes[0][2] == 67:
            t4.append(0)
        elif len(notes) == 1:
            t4.append(2)
        elif notes[1][0] == 6:
            t4.append(1)
        else:
            t4.append(1)   # E: B's notes
        continue
    held = [n for s, d, n in P["lead"].get(b - 1, []) if s + d > 16]   # a note of the bar before, held into this one
    t4.append({67: 0, 64: 2}.get(held[-1], OFF) if held else OFF)

score = {
    "name": "MURIYA",
    "bpm": BPM,
    "swing": 0,
    "bus1": [92, 88, 48, None],
    "bus2": [None, 46, 40, None],
    "scenes": [[a, b, c, d] for a, b, c, d in zip(t1, t2, t3, t4)],
    "tracks": [
        {"engine": "KANL", "color": 1, "level": 100, "pan": 50, "send1": 12,
         "params": {"WAVE1": 0, "WAVE2": 0, "DTN": 10, "LVL1": 112, "LVL2": 100, "SUB": 64, "CUT": 30, "RES": 54, "ENV": 88,
                    "DRV": 24, "KTR": 30, "VEL": 90},
         "env1": [0, 20, 22, 8], "patterns": t1_pat},
        {"engine": "KPHS", "color": 6, "level": 60, "pan": 44, "send1": 70,
         "params": {"WAVE1": 0, "WAVE2": 3, "DCW1": 72, "DCW2": 56, "DCA2": 64, "DTN": 18},
         "env1": [45, 60, 85, 30], "env2": [40, 60, 70, 30],
         "fx": [2, 22, 30, 32, 10, 18, 32, 40], "lfo": [16, 1, 0, 0, 0, 0, 8, 0], "patterns": pad_pat},
        {"engine": "KFM2", "color": 4, "level": 70, "pan": 60, "send1": 48, "send2": 34,
         "params": {"RATIO": 1, "INDEX": 54, "ENV": 78, "FEED": 10, "LEVEL": 110},
         "env1": [0, 50, 25, 35],
         "fx": [11, 40, 18, 50, 2, 30, 20, 24], "patterns": piano_pat},
        {"engine": "KTRI", "color": 2, "level": 62, "pan": 52, "send1": 64, "send2": 26,
         "params": {"W1": 1, "W2": 1, "W3": 0, "INT2": 12, "INT3": 0, "DTN": 14, "LVL2": 64, "LVL3": 40, "CUT": 92, "RES": 30},
         "env1": [30, 50, 80, 40],
         "fx": [3, 58, 42, 70], "lfo": [56, 0, 0, 80, 1, 0, 0, 0], "patterns": lead_pat},
    ],
}
OUT.write_text(json.dumps(score, indent=1) + "\n", encoding="utf-8")
print(f"{OUT}: {BPM} BPM, {len(score['scenes'])} scenes (bars {first}..{last})")
for t, name in enumerate(("bass", "pad", "piano", "lead")):
    print(f"  {name:6}", "".join("." if s[t] == OFF else str(s[t] + 1) for s in score["scenes"]))

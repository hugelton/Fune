#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Design rule 2 kept: no UI or platform code writes a value directly.

Values change only through the core's commands (k_dispatch, k_param_set, k_ref_set, k_param_reset, k_mute_toggle, ...): a knob, a
mouse, a pad, a finger and MIDI alike. This scans the UI (src/ui/*.c) and the SDL platform (platforms/sdl/*.c) for
  - an assignment (=, +=, ++, ...) to a value KApp keeps (params, lane_p, bpm, the song, a track's engine, ...),
  - a write through k_param() / k_param_at() (*k_param(...) = v),
  - a writable KVal pointer taken from them or from a->params (KVal *p = ...; const KVal * is fine),
  - memcpy / memset / memmove into such a value,
and fails on any not in ALLOW below (each with its reason). UI model state the UI owns (focused, touched, touch_ms, menu_cols)
is not a value and is not checked.

  check_ui_writes.py [repo root]      (ctest: kreisel_ui_writes)
"""
import os
import re
import sys

# KApp's values: what a song, a sound, the transport or the device's settings keep (src/kreisel.h)
VALUES = ("params lane_p plug_lane plug_lane_uid bpm swing clock_src chance chance_ui steps roll rnote rlen rvel cur tcolor mute solo "
          "lane_on lane_pat cur_lane song sound eng_mem eng_mem_set missing master octave preset_dirty preset_slot factory preset_name "
          "set_v song_name song_from pattern track page playing recording tool_x tool_y").split()
APPS = r"(?:a|app|A|hue_app|sk_app|b)"   # the names a KApp goes by in these files

# (file, value) -> why it may write it. Keep it short: each one is a loader of the device's own files, not a control.
ALLOW = {
    ("platforms/sdl/ioconf.c", "set_v"): "io.conf's [midi] in / channel at start: the device's settings read from its file "
                                         "(no command sets a setting outright; as k_settings_load does)",
    ("platforms/sdl/desk.c", "master"): "session.txt at start: the desktop's last output level, read back as a file loader",
    ("platforms/sdl/desk.c", "octave"): "session.txt at start: the last octave",
    ("platforms/sdl/desk.c", "track"): "session.txt at start: the track in hand over the song it went with",
    ("platforms/sdl/desk.c", "song_from"): "session.txt at start: the slot the song came from",
}

FIELD = r"(?P<f>" + "|".join(VALUES) + r")\b"
LVALUE = re.compile(r"\b" + APPS + r"\s*(?:->|\.)\s*" + FIELD + r"(?P<rest>(?:\s*\[[^\]]*\])*(?:\s*\.\s*\w+(?:\s*\[[^\]]*\])*)?)\s*"
                    r"(?:[-+*/%&|^]|<<|>>)?=(?!=)")
INCDEC = re.compile(r"(?:\+\+|--)\s*" + APPS + r"\s*(?:->|\.)\s*" + FIELD + r"|\b" + APPS + r"\s*(?:->|\.)\s*" + FIELD.replace("?P<f>", "?P<g>") +
                    r"(?:\s*\[[^\]]*\])*\s*(?:\+\+|--)")
DEREF = re.compile(r"\*\s*k_param(?:_at)?\s*\((?:[^()]|\([^()]*\))*\)\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)")
ALIAS = re.compile(r"(?<!const )\bKVal\s*\*\s*\w+\s*=\s*(?:k_param(?:_at)?\s*\(|&?\s*" + APPS + r"\s*->\s*(?:params|lane_p|plug_lane)\b)")
COPY = re.compile(r"\b(?:memcpy|memset|memmove)\s*\(\s*&?\s*" + APPS + r"\s*(?:->|\.)\s*" + FIELD)


def strip(src):
    """the code without its comments and strings (each kept as spaces, so the lines stay where they are)"""
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("/*", i):
            j = src.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in src[i:j])); i = j
        elif src.startswith("//", i):
            j = src.find("\n", i); j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(c + " " * (min(j, n) - i - 1) + c); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)


def scan(root):
    files = []
    for d in ("src/ui", "platforms/sdl"):
        p = os.path.join(root, d)
        files += sorted(os.path.join(d, f) for f in os.listdir(p) if f.endswith(".c"))
    bad, used = [], set()
    for rel in files:
        with open(os.path.join(root, rel), encoding="utf-8") as fh:
            code = strip(fh.read())
        for ln, line in enumerate(code.split("\n"), 1):
            hits = []
            for m in LVALUE.finditer(line): hits.append(m.group("f"))
            for m in INCDEC.finditer(line): hits.append(m.group("f") or m.group("g"))
            for m in COPY.finditer(line): hits.append(m.group("f"))
            if DEREF.search(line): hits.append("*k_param()")
            if ALIAS.search(line): hits.append("KVal * (writable)")
            for f in hits:
                if (rel, f) in ALLOW: used.add((rel, f)); continue
                bad.append("%s:%d: writes %s directly: %s" % (rel, ln, f, line.strip()))
    return bad, set(ALLOW) - used


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    bad, stale = scan(root)
    for b in bad: print(b)
    for f in sorted(stale): print("ALLOW %s %s: no such write any more (take it out of tools/check_ui_writes.py)" % f)
    if bad:
        print("UI code writes values directly: go through the core (k_param_set, k_ref_set, k_dispatch ...) or, with a reason, ALLOW")
    return 1 if bad or stale else 0


if __name__ == "__main__":
    sys.exit(main())

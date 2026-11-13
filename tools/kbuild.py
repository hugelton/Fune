#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Read platforms/platforms.toml (the source groups) and platforms/targets/*.toml (each target: what it builds, its screen, its
controls). See platforms/targets/README.md. Python 3.11+ (tomllib).

    kbuild.py TARGET sources|defines|cflags|profile      (space-separated, for a shell)
    kbuild.py --group NAME [--cmake]                      (one group; --cmake: ';'-separated, for CMake)
    kbuild.py --profiles [--check]                        (src/ui/profiles_gen.h; --check: fail if it is not current)
    kbuild.py --list                                      (the targets: name, status, screen, inputs)"""
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "src" / "ui" / "profiles_gen.h"
INPUTS = {"knobs": "K_IN_KNOBS", "keys": "K_IN_KEYS", "pad": "K_IN_PAD", "touch": "K_IN_TOUCH", "keyboard": "K_IN_KEYBOARD",
          "mouse": "K_IN_MOUSE"}
LAYOUT = {"leds": "K_LAY_LEDS", "usb_audio": "K_LAY_USB_AUDIO", "preset_pop": "K_LAY_PRESET_POP", "connect": "K_LAY_CONNECT", "sheet_cursor": "K_LAY_SHEET_CURSOR", "sheet_pointer": "K_LAY_SHEET_POINTER"}
FONTS = {"vector": "K_FONT_VECTOR", "bitmap": "K_FONT_BITMAP"}
STATUS = ("target", "fixture", "kept")
KEY_ROLES = 7   # K_KEY_ROLES (src/ui/draw.h): ARM1..4, OK, CANCEL, SAVE


def fail(msg):
    sys.exit("kbuild.py: " + msg)


def load():
    with open(ROOT / "platforms" / "platforms.toml", "rb") as f:
        cfg = tomllib.load(f)
    targets = {}
    for p in sorted((ROOT / "platforms" / "targets").glob("*.toml")):
        with open(p, "rb") as f:
            t = tomllib.load(f)
        if t.get("status") not in STATUS:
            fail("%s: status is one of %s" % (p.name, ", ".join(STATUS)))
        targets[p.stem] = t
    cfg["targets"] = targets
    return cfg


def c_str(s):
    if not all(32 <= ord(ch) < 127 and ch not in '"\\' for ch in s):
        fail("%r: ASCII, no quotes" % s)
    return '"%s"' % s


def flags(names, table, where):
    for n in names:
        if n not in table:
            fail("%s: %r is not one of %s" % (where, n, ", ".join(table)))
    return " | ".join(table[n] for n in names) if names else "0"


def profile_row(name, t):
    sc, co, lay = t.get("screen"), t.get("controls", {}), t.get("layout", {})
    if not sc:
        fail("%s: a profile needs [screen]" % name)
    w, h = sc.get("size", [0, 0])
    pw, ph = sc.get("pixels", [0, 0])
    diag = float(sc.get("diag", 0))
    if not (w and h) and not (pw and ph and diag):
        fail("%s: [screen] size = [w, h], or pixels = [w, h] and diag" % name)
    keys = co.get("keys")
    keys_ref = "0"
    if keys:
        if len(keys) != KEY_ROLES:
            fail("%s: keys: %d names (one a role)" % (name, KEY_ROLES))
        keys_ref = "KEYS_%s" % name.upper()
    row = '    {%s, %d, %d, %s, %d, %d, %d, %d, %.1ff, %s, %s, %s, %s},' % (
        c_str(name), w, h, flags(co.get("input", []), INPUTS, name + " input"), co.get("knobs", 0), co.get("shown", 4),
        pw, ph, diag, FONTS[sc.get("font", "vector")], keys_ref, flags(lay.get("add", []), LAYOUT, name + " layout.add"),
        flags(lay.get("drop", []), LAYOUT, name + " layout.drop"))
    return row, (keys_ref, keys) if keys else None


def profiles_text(cfg):
    order = cfg["profiles"]["order"]
    rows, keys = [], []
    for n in order:
        if n not in cfg["targets"]:
            fail("[profiles] order: no platforms/targets/%s.toml" % n)
        row, k = profile_row(n, cfg["targets"][n])
        rows.append(row + "   /* %s (%s) */" % (cfg["targets"][n].get("name", n), cfg["targets"][n]["status"]))
        if k:
            keys.append("static const char *const %s[K_KEY_ROLES] = {%s};" % (k[0], ", ".join(c_str(x) for x in k[1])))
    for n, t in cfg["targets"].items():
        if "screen" in t and n not in order:
            fail("%s has a [screen] but is not in [profiles] order" % n)
    return ("/* Made by tools/kbuild.py --profiles from platforms/targets/: do not edit (change the target's file, run it).\n"
            " * Included once, by src/ui/draw.c. */\n" + "\n".join(keys) + "\nstatic const KProfile PROFILES[] = {\n" +
            "\n".join(rows) + "\n};\n")


def target(cfg, name):
    t = cfg["targets"].get(name)
    if t is None:
        fail("no target %r (%s)" % (name, ", ".join(cfg["targets"])))
    return t


def main(argv):
    cfg = load()
    groups = cfg["groups"]
    if len(argv) >= 2 and argv[0] == "--group":
        print((";" if "--cmake" in argv else " ").join(groups[argv[1]]))
        return 0
    if argv and argv[0] == "--profiles":
        text = profiles_text(cfg)
        if "--check" in argv:
            if not GEN.exists() or GEN.read_text() != text:
                fail("%s is not current: run tools/kbuild.py --profiles" % GEN.relative_to(ROOT))
            return 0
        GEN.write_text(text)
        return 0
    if argv and argv[0] == "--list":
        for n, t in cfg["targets"].items():
            sc = t.get("screen", {})
            size = "x".join(map(str, sc.get("size", sc.get("pixels", [])))) or ("(profile %s)" % t.get("profile", "-"))
            print("%-10s %-8s %-10s %-28s %s" % (n, t["status"], size, ",".join(t.get("controls", {}).get("input", [])), t.get("name", "")))
        return 0
    if len(argv) != 2:
        sys.exit(__doc__)
    name, what = argv
    t = target(cfg, name)
    b = t.get("build", {})
    if what == "sources":
        out = [f for g in b.get("groups", []) for f in groups[g]] + b.get("sources", [])
        for f in out:
            if not (ROOT / f).exists():
                fail("%s: no %s" % (name, f))
    elif what == "defines":
        out = ["-D%s" % k if v == "" else "-D%s=%s" % (k, v) for k, v in b.get("defines", {}).items()]
    elif what == "cflags":
        out = b.get("cflags", [])
    elif what == "profile":
        out = [t.get("profile", name if "screen" in t else "")]
    else:
        sys.exit(__doc__)
    print(" ".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

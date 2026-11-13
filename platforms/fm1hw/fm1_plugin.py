#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Outside plugins on the FM-1, over its USB serial console (fm1_console.c: plugs, plugput, plugdata, plugend, plugoff, plugon,
plugerase; fm1_plugins.c). A plugin for the FM-1 is a .kpl file (tools/kpl_build.py; the format: src/kreisel_plugin_image.h).

  ~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py list
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py install build/fm1/plugins/beep.kpl
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py off 1          # from the next start (a bit of its header, no erase)
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py on 1
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py erase 1        # not one that is live: off, restart, then erase
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_plugin.py check FILE.kpl # what the file is (no device)

install: the FM-1 names where the image goes and its RAM (plugput), this links the file for them, sends it (plugdata, a CRC-32
over all of it) and the FM-1 writes the header's magic last, after reading the image back (plugend). It plays at once unless a
plugin of the same id is live (then from the next start: the newest version of an id wins; erase the old one after). The FM-1
refuses while it plays. --port as fm1_con.py."""
import argparse
import re
import struct
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent.parent / "tools"))
from fm1_con import ask, open_console  # noqa: E402
from kpl_build import HDR, HDR_FMT, HDR_KEYS, RELOC_RAM, hcrc, info, parse, uid_str  # noqa: E402

LINE = 27   # bytes a plugdata line (a console line holds 63 characters)


def fail(msg):
    sys.exit(f"fm1_plugin: {msg}")


def load(path):
    data = Path(path).read_bytes()
    try:
        h = parse(data)
    except ValueError as e:
        fail(f"{path}: {e}")
    if h["base"]:
        fail(f"{path}: linked already (a .kpl as tools/kpl_build.py makes it is not)")
    return data, h


def link(data, h, base, ram):
    """the file linked for base and ram: the image, its header sealed (k_image_link does the same)"""
    size = h["size"]
    img = bytearray(data[:size])
    for i in range(h["nreloc"]):
        r = struct.unpack_from("<I", data, size + 4 * i)[0]
        off = r & 0x3FFFFFFF
        if off < HDR or off > size - 4 or (r & 0x40000000):
            fail("a relocation outside the image")
        if r & RELOC_RAM and not ram:
            fail("a relocation of RAM, but the FM-1 gave none")
        struct.pack_into("<I", img, off, (struct.unpack_from("<I", img, off)[0] + (ram if r & RELOC_RAM else base)) & 0xFFFFFFFF)
    v = list(struct.unpack_from(HDR_FMT, img))
    v[HDR_KEYS.index("base")] = base
    v[HDR_KEYS.index("ram")] = ram if h["data_size"] + h["bss_size"] else 0
    v[HDR_KEYS.index("nreloc")] = 0
    v[HDR_KEYS.index("crc")] = zlib.crc32(bytes(img[HDR:]))
    struct.pack_into(HDR_FMT, img, 0, *v)
    struct.pack_into("<I", img, 60, hcrc(img))
    return bytes(img)


def cmd_list(s):
    r = ask(s, "plugs")
    m = re.match(r"plugs n=(\d+)", r)
    if not m:
        fail(r or "no reply")
    print(r)
    for _ in range(int(m.group(1))):   # (a line an image follows)
        print(read_line(s))


def read_line(s, timeout=1.5):
    end = time.time() + timeout
    line = b""
    while time.time() < end:
        line += s.read(1)
        if line.endswith(b"\n"):
            break
    return line.decode("ascii", "replace").strip()


def cmd_install(s, path):
    data, h = load(path)
    need = h["data_size"] + h["bss_size"]
    r = ask(s, f"plugput {h['size']} {need}", timeout=8.0)   # (erases its sectors: up to ~0.4 s each)
    m = re.match(r"ok base=0x([0-9A-Fa-f]{8}) ram=0x([0-9A-Fa-f]{8})", r)
    if not m:
        fail(f"plugput: {r or 'no reply'}")
    base, ram = int(m.group(1), 16), int(m.group(2), 16)
    img = link(data, h, base, ram)
    for i in range(0, len(img), LINE):
        r = ask(s, "plugdata " + img[i:i + LINE].hex())
        if r != f"ok n={min(i + LINE, len(img))}":
            fail(f"plugdata at {i}: {r or 'no reply'}")
    r = ask(s, f"plugend 0x{zlib.crc32(img):08X}", timeout=5.0)
    if not r.startswith("ok"):
        fail(f"plugend: {r or 'no reply'}")
    print(f"{path} ('{uid_str(h['uid'])}' v{h['version']}, {len(img)} bytes) -> FM-1 at {base:#010x}: {r}")
    if "live=0" in r:
        print("  not live now (its id is: the newest of them from the next start); `list` says more")


def cmd_n(s, verb, n):
    r = ask(s, f"plug{verb} {n}", timeout=5.0)
    if not r.startswith("ok"):
        fail(f"plug{verb}: {r or 'no reply'}")
    print(r)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device (default: the first /dev/cu.usbmodem*)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="the plugins in the FM-1's flash")
    p = sub.add_parser("install", help="a .kpl into the FM-1")
    p.add_argument("file")
    for verb in ("off", "on", "erase"):
        p = sub.add_parser(verb, help=f"plug{verb} N (the number `list` gives)")
        p.add_argument("n", type=int)
    p = sub.add_parser("check", help="what a .kpl is (no device)")
    p.add_argument("file")
    a = ap.parse_args()
    if a.cmd == "check":
        load(a.file)
        info(a.file)
        return 0
    with open_console(a.port) as s:
        if a.cmd == "list":
            cmd_list(s)
        elif a.cmd == "install":
            cmd_install(s, a.file)
        else:
            cmd_n(s, a.cmd, a.n)
    return 0


if __name__ == "__main__":
    sys.exit(main())

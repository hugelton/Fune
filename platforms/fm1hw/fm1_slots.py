#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Songs and presets between the FM-1 and a computer, over its USB serial console (fm1_console.c: slots, slotget, slotdata,
songput, songdata, slotput). A .fune file is a slot's bytes as kept: a song ("KRSG") or a preset ("KRSP", kreisel_song.h),
the same file the desktop build opens (Fune FILE.fune, or dropped on its window). docs/TRANSFER.md.

  ~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py list
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py pull song 1 [FILE.fune]     # default: the slot's name, else song1.fune
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py push FILE.fune 3            # into song slot 3 (a preset file: preset slot 3)
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_slots.py check FILE.fune             # what the file is (no device)

push overwrites the slot (the song playing on the FM-1 is not changed: LOAD it there). The FM-1 refuses while it plays, and
anything its loader would refuse; each transfer is checked by a CRC-32 both ways. --port as fm1_con.py."""
import argparse
import re
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_con import ask, open_console  # noqa: E402

SLOTS = {"song": 6, "preset": 24}
KIND_OF = {b"KRSG": "song", b"KRSP": "preset"}


def crc16(b):  # CCITT, 0xFFFF start (kreisel_song.c)
    c = 0xFFFF
    for x in b:
        c ^= x << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def file_info(data):
    """(kind, name, the file's own bytes) of a song or preset file, or ValueError: its header, CRC-16 and chunks as the loader
    checks them (the FM-1 checks again, with the loader itself)."""
    if len(data) < 10 or data[:4] not in KIND_OF:
        raise ValueError("not a Fune song or preset (no KRSG / KRSP header)")
    ver, plen = int.from_bytes(data[4:6], "little"), int.from_bytes(data[6:8], "little")
    if ver < 1 or ver > 1:
        raise ValueError(f"format version {ver}: not one this tool knows")
    end = 8 + plen
    if end + 2 > len(data):
        raise ValueError("cut short")
    if crc16(data[:end]) != int.from_bytes(data[end:end + 2], "little"):
        raise ValueError("damaged (its CRC does not match)")
    pos, name, trak = 8, "", False
    while pos < end:
        if end - pos < 6:
            raise ValueError("damaged (a chunk cut short)")
        tag, n = data[pos:pos + 4], int.from_bytes(data[pos + 4:pos + 6], "little")
        if pos + 6 + n > end:
            raise ValueError("damaged (a chunk past the end)")
        if tag == b"NAME":
            name = data[pos + 6:pos + 6 + n].decode("ascii", "replace")
        if tag == b"TRAK" and n >= 9:
            trak = True
        pos += 6 + n
    kind = KIND_OF[data[:4]]
    if kind == "preset" and not trak:
        raise ValueError("a preset with no engine in it")
    return kind, name, data[:end + 2]


def fail(msg):
    sys.exit(f"fm1_slots: {msg}")


def cmd_list(s):
    r = ask(s, "slots")
    if not r.startswith("ok"):
        fail(r or "no reply")
    for kind, short in (("song", "s"), ("preset", "p")):
        print(f"{kind}s:")
        for m in re.finditer(rf"\b{short}(\d+)=(\S+)", r):
            v = m.group(2)
            print(f"  {int(m.group(1)):2d}  {'(empty)' if v == '-' else '(no name)' if v == '*' else v.replace('_', ' ')}")


def cmd_pull(s, kind, slot, out):
    r = ask(s, f"slotget {kind} {slot}")
    m = re.match(r"ok bytes=(\d+) crc=0x([0-9A-Fa-f]{8})", r)
    if not m:
        fail(f"slotget: {r or 'no reply'}")
    total, crc = int(m.group(1)), int(m.group(2), 16)
    data = bytearray()
    while len(data) < total:
        r = ask(s, f"slotdata {len(data)}")
        m = re.match(r"ok (\d+) ([0-9A-Fa-f]+)$", r)
        if not m or int(m.group(1)) != len(data):
            fail(f"slotdata {len(data)}: {r or 'no reply'}")
        data += bytes.fromhex(m.group(2))
    data = bytes(data)
    if len(data) != total or zlib.crc32(data) != crc:
        fail("the bytes came over damaged (CRC-32): try again")
    k, name, _ = file_info(data)
    if k != kind:
        fail(f"slot {slot} holds a {k}, not a {kind}")
    path = Path(out) if out else Path(f"{re.sub(r'[^A-Za-z0-9.-]+', '_', name).strip('_') or kind + str(slot)}.fune")
    path.write_bytes(data)
    print(f"{kind} {slot} ({name or 'no name'}, {total} bytes) -> {path}")


def cmd_push(s, path, slot, kind_arg):
    try:
        kind, name, data = file_info(Path(path).read_bytes())
    except ValueError as e:
        fail(f"{path}: {e}")
    if kind_arg and kind_arg != kind:
        fail(f"{path} is a {kind}, not a {kind_arg}")
    if not 1 <= slot <= SLOTS[kind]:
        fail(f"a {kind} slot is 1..{SLOTS[kind]}")
    r = ask(s, f"songput {len(data)}")
    if r != "ok":
        fail(f"songput: {r or 'no reply'}")
    for i in range(0, len(data), 24):  # (a console line holds 64 characters)
        r = ask(s, "songdata " + data[i:i + 24].hex())
        if r != f"ok n={min(i + 24, len(data))}":
            fail(f"songdata at {i}: {r or 'no reply'}")
    r = ask(s, f"slotput {kind} {slot} 0x{zlib.crc32(data):08X}", timeout=5.0)  # (a flash erase and write)
    if not r.startswith("ok"):
        fail(f"slotput: {r or 'no reply'}")
    print(f"{path} ({name or 'no name'}, {len(data)} bytes) -> {kind} {slot}: {r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial device (default: the first /dev/cu.usbmodem*)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="the slots and their names")
    p = sub.add_parser("pull", help="a slot to a .fune file")
    p.add_argument("kind", choices=SLOTS)
    p.add_argument("slot", type=int)
    p.add_argument("file", nargs="?")
    p = sub.add_parser("push", help="a .fune file to a slot (of the file's kind)")
    p.add_argument("file")
    p.add_argument("slot", type=int)
    p.add_argument("--kind", choices=SLOTS, help="refuse unless the file is this kind")
    p = sub.add_parser("check", help="what a .fune file is (no device)")
    p.add_argument("file")
    a = ap.parse_args()
    if a.cmd == "check":
        try:
            kind, name, data = file_info(Path(a.file).read_bytes())
        except ValueError as e:
            fail(f"{a.file}: {e}")
        print(f"{a.file}: {kind}, {name or 'no name'}, {len(data)} bytes")
        return 0
    if a.cmd == "pull" and not 1 <= a.slot <= SLOTS[a.kind]:
        fail(f"a {a.kind} slot is 1..{SLOTS[a.kind]}")
    with open_console(a.port) as s:
        if a.cmd == "list":
            cmd_list(s)
        elif a.cmd == "pull":
            cmd_pull(s, a.kind, a.slot, a.file)
        else:
            cmd_push(s, a.file, a.slot, a.kind)
    return 0


if __name__ == "__main__":
    sys.exit(main())

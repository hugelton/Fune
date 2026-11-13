#!/usr/bin/env python3
"""Build a portable KRSG song file from the editable COLDWIRE score.

Keep this independent of the synthesizer UI. It uses the version-1 KRSG
chunk layout in src/kreisel_song.c; --check detects stale generated files.
"""
import argparse
import binascii
import json
import struct
from pathlib import Path

PIECES = {name: i for i, name in enumerate((   # DRUM's pieces, in src/engines_data.c's order (KN_DRUM_N_PIECE)
    "T8 BD", "T9 BD", "K5 BD", "T8 SD", "T9 SD", "K7 SD", "T8 TOM", "T9 TOM", "K7 TOM", "T8 CNG", "T8 RS", "T9 RS", "K5 RS",
    "T8 CL", "K5 CL", "T8 CP", "K7 CP", "T8 CB", "K5 CB", "T8 MA", "V1 SHA", "T8 CH", "T8 OH", "K5 CH", "K5 OH", "K7 CH",
    "K7 OH", "K5 CY", "K7 CY", "V1 PO", "V1 PI", "SP BD", "SP CH", "SP OH", "TP ZP"))}


def param_id(name):
    h = 2166136261
    for byte in name.encode("ascii"):
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return ((h >> 16) ^ (h & 0xFFFF)) or 1


def chunk(tag, data):
    assert len(tag) == 4 and len(data) <= 0xFFFF
    return tag.encode("ascii") + struct.pack("<H", len(data)) + bytes(data)


def make_song(score):
    tracks = score["tracks"]
    assert len(tracks) == 4 and 1 <= len(score["scenes"]) <= 256
    assert all(len(scene) == 4 and all(0 <= p <= 4 for p in scene) for scene in score["scenes"])
    assert len(score["name"]) <= 10 and score["name"].encode("ascii").decode("ascii") == score["name"]

    parts = [
        chunk("GLOB", [score["bpm"], score["swing"], 0, 0, 0, 4]),
        chunk("SCNS", [4, 0] + [(scene[t] & 15) | (scene[t + 1] & 15) << 4 for scene in score["scenes"] for t in (0, 2)]),   # (4 tracks, two cells a byte)
        chunk("NAME", score["name"].encode("ascii")),
    ]

    for t, track in enumerate(tracks):
        uid = int.from_bytes(track["engine"].encode("ascii"), "big")
        params = track.get("params", {})
        trk = bytearray([t])
        trk.extend(struct.pack("<I", uid))
        trk.extend([1, track["color"], 0, len(params)])
        for name, value in params.items():
            assert -128 <= value <= 127
            trk.extend(struct.pack("<HB", param_id(name), value + 128))
        parts.append(chunk("TRAK", trk))

        notes = bytearray()
        for pattern, events in enumerate(track.get("patterns", [])):
            assert pattern < 4
            for step, note, length, velocity in events:
                assert 1 <= step <= 16 and 1 <= length <= 17 - step
                assert 0 <= note <= 127 and 1 <= velocity <= 127
                notes.extend([pattern * 16 + step - 1, length, note, velocity])
        assert len(notes) // 4 <= 64
        parts.append(chunk("ROLL", bytes([t, len(notes) // 4]) + notes))

        if "lanes" in track:
            masks = [0] * 8
            lanes = [[0, 64, 64, 64, 100, 0, 50, 50, 0, 50] for _ in range(8)]
            for entry in track["lanes"]:
                lane = entry["lane"]
                assert 0 <= lane < 8 and masks[lane] == 0
                assert all(1 <= s <= 16 for s in entry["steps"])
                assert entry["piece"] in PIECES
                masks[lane] = sum(1 << (step - 1) for step in set(entry["steps"]))
                values = lanes[lane]
                values[0] = PIECES[entry["piece"]]
                for j, key in enumerate(("tune", "tone", "decay", "level",
                                          "attack", "pdep", "ptim", "click", "pan"), 1):
                    if key in entry:
                        values[j] = entry[key]
                assert all(0 <= v <= 127 for v in values)
            drum = bytearray([t])
            drum.extend(b"".join(struct.pack("<H", mask) for mask in masks))
            drum.extend(v for lane in lanes for v in lane)
            parts.append(chunk("DRUM", drum))

        prms = bytearray([t, 1, 0, track["level"] + 128])
        for j, key in ((1, "pan"), (2, "send1"), (3, "send2")):   # the mixer: pan, the sends to BUS 1 (reverb) and BUS 2 (delay)
            if key in track:
                assert 0 <= track[key] <= 100
                prms.extend([1, j, track[key] + 128])
        if t == 0:   # the buses are the song's, kept with track 1: BUS 1 SIZE DCY TONE RTN, BUS 2 TIME FB TONE RTN
            for page, key in ((6, "bus1"), (7, "bus2")):
                for j, value in enumerate(score.get(key, [])):
                    if value is None:
                        continue
                    assert j < 4 and 0 <= value <= 100
                    prms.extend([page, j, value + 128])
        for page, key in ((3, "env1"), (4, "env2")):
            for j, value in enumerate(track.get(key, [])):
                assert j < 4 and 0 <= value <= 100
                prms.extend([page, j, value + 128])
        parts.append(chunk("PRMS", prms))

    payload = b"".join(parts)
    assert len(payload) <= 0xFFFF
    header = b"KRSG" + struct.pack("<HH", 1, len(payload))
    data = header + payload
    data += struct.pack("<H", binascii.crc_hqx(data, 0xFFFF))
    assert len(data) <= 3072  # four-track FM-1 K_SAVE_BUF
    return data


def c_source(data):
    """src/kreisel_demo.c: the song in the firmware, loaded at start while developing"""
    rows = ["    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
    return ("/* SPDX-License-Identifier: MPL-2.0\n * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), H\u00fcgelton Instruments */\n"
            "/* COLDWIRE, the demo song (examples/coldwire.json): written by tools/make_demo_song.py, do not edit. Loaded at start while\n"
            " * KREISEL_DEMO_SONG is 1 (the default during development: kreisel_demo.h) */\n"
            "#include \"kreisel_demo.h\"\n#include \"kreisel_song.h\"\n\n"
            f"static const unsigned char COLDWIRE[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n\n"
            "int k_demo_load(KApp *a) { return k_song_load(a, COLDWIRE, sizeof COLDWIRE) == K_SONG_OK ? 0 : -1; }\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("score", nargs="?", type=Path, default=Path("examples/coldwire.json"))
    parser.add_argument("output", nargs="?", type=Path, default=Path("examples/coldwire.krs"))
    parser.add_argument("--c", type=Path, default=Path("src/kreisel_demo.c"), help="the C copy the apps load at start")
    parser.add_argument("--check", action="store_true", help="verify the committed KRSG (and its C copy) against the score")
    args = parser.parse_args()
    data = make_song(json.loads(args.score.read_text(encoding="utf-8")))
    src = c_source(data).encode("utf-8")
    if args.check:
        if not args.output.is_file() or args.output.read_bytes() != data:
            parser.error("song file does not match the score: regenerate it")
        if not args.c.is_file() or args.c.read_bytes() != src:
            parser.error(f"{args.c} does not match the score: regenerate it")
    else:
        args.output.write_bytes(data)
        args.c.write_bytes(src)
    print(f"{args.output}, {args.c}: {len(data)} bytes, {'checked' if args.check else 'written'}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Put a song file (.krs) into the FM-1 over its USB serial console (fm1_console.c: songput, songdata, songload), then keep it in
a song slot (save N). The song in the FM-1 is replaced: run `save` first if it should be kept.
  ~/fm1-venv/bin/python platforms/fm1hw/fm1_put_song.py examples/coldwire.krs 6 [--port /dev/cu.usbmodem...]"""
import argparse, subprocess, sys
from pathlib import Path

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("song", type=Path)
    ap.add_argument("slot", type=int, choices=range(1, 7), help="the song slot to keep it in (1..6)")
    ap.add_argument("--port", default=None)
    a = ap.parse_args()
    data = a.song.read_bytes()
    cmds = [f"songput {len(data)}"] + [f"songdata {data[i:i + 24].hex()}" for i in range(0, len(data), 24)] + ["songload", f"save {a.slot}"]
    con = [sys.executable, str(Path(__file__).with_name("fm1_con.py"))] + (["--port", a.port] if a.port else []) + cmds
    out = subprocess.run(con, capture_output=True, text=True)
    lines = out.stdout.strip().splitlines()
    for line in lines[-2:]: print(line)
    bad = [l for l in lines if ": err" in l or l.startswith("err")]
    if bad or out.returncode: print("\n".join(bad) or out.stderr, file=sys.stderr); sys.exit(1)

if __name__ == "__main__":
    main()

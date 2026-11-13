#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""fm1_diag: read Kreisel's bring-up diagnostics from an FM-1 over USB-MIDI (platforms/fm1hw/fm1_main.c).

In SAFE mode (after a fault, or two boots that never got healthy) Kreisel sends every 500 ms, and while it runs
(KREISEL_FM1_DIAG, on during bring-up) every second,
F0 7D 4B 44 <stage> <safe> <reason> <the fault record: 11 words, the 12 stage start times (us), the longest frame (us), this boot's reset, the boot before's stage and reset, frames, uptime ms, the boot before's own reset request, the last and mean frame, the audio interrupt's worst, halves, over-budget run, on: 36 words, 5 x 7 bits each> F7.
  fm1_diag.py [--port NAME] [--wait S]     prints the stage, the reason and the record; exit 0 got one, 3 none
Needs mido with python-rtmidi.
"""
import argparse
import sys
import time

STAGES = ["?", "cstart", "lcd", "input", "usb", "irq", "ui_init", "k_init", "audio", "loop", "frame", "healthy"]
FIELDS = ["magic", "count", "vec", "pc", "rets", "emu", "dbg", "sp", "psr", "icfg", "uptime_ms"]
REASONS = {0: "none", 1: "a fault", 2: "a boot loop (two boots never healthy)"}


def main():
    import mido
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="MIDI input port (part of its name); default: the first FM-1 / Kreisel / Felucca one")
    ap.add_argument("--wait", type=float, default=3.0)
    a = ap.parse_args()
    names = mido.get_input_names()
    want = [n for n in names if (a.port and a.port in n) or (not a.port and any(k in n for k in ("FM-1", "Kreisel", "Felucca")))]
    if not want:
        print(f"fm1_diag: no port (inputs: {names})")
        return 3
    with mido.open_input(want[0]) as p:
        end = time.time() + a.wait
        while time.time() < end:
            for m in p.iter_pending():
                d = list(m.data) if m.type == "sysex" else []
                if d[:3] == [0x7D, 0x4B, 0x44] and len(d) >= 6 + 30 * 5:
                    stage, safe, reason = d[3], d[4], d[5]
                    nw = min(36, (len(d) - 6) // 5)
                    w = [0] * 36
                    for i in range(nw):
                        v = 0
                        for k in range(5):
                            v = (v << 7) | d[6 + 5 * i + k]
                        w[i] = v & 0xFFFFFFFF
                    print(f"port {want[0]}: stage {stage} ({STAGES[stage] if stage < len(STAGES) else '?'}), "
                          f"safe {safe}, reason {REASONS.get(reason, reason)}")
                    for f, v in zip(FIELDS, w):
                        print(f"  {f:9s} 0x{v:08x}")
                    print("  stage start (ms): " + ", ".join(f"{STAGES[i]} {w[11 + i] / 1000:.1f}" for i in range(1, 12)
                                                              if w[11 + i] or i == 1))
                    print(f"  longest frame: {w[23] / 1000:.1f} ms; last {w[30] / 1000:.1f} ms; mean {w[31] / 1000:.1f} ms; "
                          f"frames drawn {w[27]}; up {w[28] / 1000:.1f} s")
                    if nw >= 36:
                        print(f"  audio: worst interrupt {w[32]} us (budget ~5805 us a half), halves {w[33]}, "
                              f"over-budget run {w[34]}, rendering {'on' if w[35] & 1 else 'OFF (the load guard stopped it)'}, peak output {w[35] >> 1} of 16384")
                    def rs(v):
                        p3 = v & 0xFF
                        why = [n for b, n in ((0, "power-on"), (1, "VDDIO low"), (2, "WDT"), (3, "VCM"), (4, "long press"),
                                              (5, "1.2 V"), (6, "soft")) if p3 >> b & 1]
                        return f"0x{v:06x} ({', '.join(why) or 'none'}{', soft PWR' if v >> 8 & 1 else ''})"
                    print(f"  this boot's reset {rs(w[24])}")
                    print(f"  the boot before: stopped at stage {w[25]} ({STAGES[w[25]] if w[25] < len(STAGES) else '?'}), "
                          f"reset {rs(w[26])}, asked by Kreisel: "
                          f"{ {0: 'no (a watchdog, the BSP or power)', 1: 'a fault', 2: 'an update', 3: 'UBOOT'}.get(w[29], w[29])}")
                    if w[0] != 0x43525348:
                        print("  (no fault record: magic is not CRSH)")
                    return 0
            time.sleep(0.02)
    print(f"fm1_diag: no diagnostics on {want[0]} in {a.wait:.0f} s (not in SAFE mode?)")
    return 3


if __name__ == "__main__":
    sys.exit(main())

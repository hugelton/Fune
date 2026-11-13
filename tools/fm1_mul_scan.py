#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The FM-1's CPU erratum (third_party/felucca-bsp/docs/CPU.md): a 32x32->64 multiply inlined among 16-bit loads now and
then leaves a register off. This lists, per function of the FM-1 image, its 64-bit multiplies and how many sit within a few
instructions of a 16-bit load; exit 1 when any does (src/kreisel_mul.h keeps them out of line).

    fm1_mul_scan.py kreisel.dis kreisel.sym [--all]
kreisel.dis: `objdump -d` of the image; kreisel.sym: `objdump -t` (build.sh writes both)."""
import bisect, collections, re, sys

NEAR = 6   # instructions either side
MUL = re.compile(r'r\d+_r\d+ = r\d+ \* r\d+')
H_LOAD = re.compile(r'= h\[')

def main(dis, sym, show_all):
    syms = []
    for line in open(sym):
        p = [w for w in line.split() if w != '.hidden']   # (a symbol of hidden visibility: objdump marks it)
        if len(p) >= 6 and ' F ' in line:
            syms.append((int(p[0], 16), int(p[-2], 16), p[-1]))
    syms.sort()
    starts = [s[0] for s in syms]
    per = collections.defaultdict(list)
    for line in open(dis):
        m = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)', line)
        if not m:
            continue
        a = int(m.group(1), 16)
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and a < syms[i][0] + max(syms[i][1], 1):
            per[syms[i][2]].append(m.group(2))
    bad = 0
    for f, ins in sorted(per.items()):
        muls = [i for i, l in enumerate(ins) if MUL.search(l)]
        if not muls:
            continue
        loads = [i for i, l in enumerate(ins) if H_LOAD.search(l)]
        near = sum(1 for m in muls if any(abs(m - x) <= NEAR for x in loads))
        bad += near > 0
        if near or show_all:
            print("%-28s %3d mul64, %3d beside a 16-bit load" % (f, len(muls), near))
    return 1 if bad else 0

if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2], '--all' in sys.argv))

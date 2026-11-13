#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Floating point in the FM-1 image (no FPU: every float is a soft-float library call). Lists, per function, its calls to
the soft-float helpers (__addsf3, __muldf3, __fixsfsi, ...) and libm (powf, lrintf, ...); with --objs, only functions
built from those objects, and exit 1 when any of them calls one (the drawing code draws in integers).

    fm1_float_scan.py kreisel.dis kreisel.sym kreisel.map [--objs draw.o,gfx.o,...]
kreisel.dis: `objdump -d` of the image; kreisel.sym: `objdump -t`; kreisel.map: the linker's -Map (build.sh writes all)."""
import bisect, collections, re, sys

FLOAT = re.compile(r'^(__\w*[sd]f\w*\d?|__\w*(sf|df)\d?|(a?sin|a?cos|a?tan|atan2|sqrt|pow|exp|exp2|log|log2|log10|floor|ceil|'
                   r'round|lround|rint|lrint|fabs|fmod|tanh|sinh|cosh|cbrt|hypot|trunc|modf|frexp|ldexp)f?)$')
CALL = re.compile(r'\bcall\b.*<(\w+)')

def owners(mapfile):
    own, name = {}, None
    for line in open(mapfile):
        m = re.match(r'^ \.text\.(\S+)(?:\s+0x\S+\s+0x\S+\s+(\S+\.o))?\s*$', line)
        if m:
            name = m.group(1)
            if m.group(2):
                own[name] = m.group(2).rsplit('/', 1)[-1]; name = None
            continue
        m = re.match(r'^\s+0x\S+\s+0x\S+\s+(\S+\.o)\s*$', line)
        if m and name:
            own[name] = m.group(1).rsplit('/', 1)[-1]
        name = None
    return own

def main(dis, sym, mapfile, objs):
    syms = []
    for line in open(sym):
        p = [w for w in line.split() if w != '.hidden']   # (a symbol of hidden visibility: objdump marks it)
        if len(p) >= 6 and ' F ' in line:
            syms.append((int(p[0], 16), int(p[-2], 16), p[-1]))
    syms.sort()
    starts = [s[0] for s in syms]
    calls = collections.defaultdict(collections.Counter)
    for line in open(dis):
        m = re.match(r'^\s*([0-9a-f]+):', line)
        c = CALL.search(line)
        if not m or not c or not FLOAT.match(c.group(1)):
            continue
        a = int(m.group(1), 16)
        i = bisect.bisect_right(starts, a) - 1
        if i >= 0 and a < syms[i][0] + max(syms[i][1], 1):
            calls[syms[i][2]][c.group(1)] += 1
    own = owners(mapfile)
    bad = 0
    for f, cs in sorted(calls.items()):
        o = own.get(f, '?')
        if objs and o not in objs:
            continue
        bad += 1
        print("%-12s %-28s %s" % (o, f, ' '.join("%s x%d" % kv for kv in sorted(cs.items()))))
    return 1 if objs and bad else 0

if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    objs = set()
    if '--objs' in sys.argv:
        objs = set(sys.argv[sys.argv.index('--objs') + 1].split(','))
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3], objs))

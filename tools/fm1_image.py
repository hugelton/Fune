#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Lay the linked FM-1 app out as the flat image the package carries, and check it.

  fm1_image.py BUILD_DIR OUT.bin     BUILD_DIR holds kreisel.sym (objdump -t), text.bin, data.bin, rt.bin

The image starts at 0x02000120 (the XIP map; the SPL jumps there). .ram_text (the flash driver, run from RAM) and
.data follow .text at their load addresses; fm1_cstart copies them by words. The layout is that of
third_party/felucca-bsp/examples/hwtest/build.sh. Checks: the entry point, the slot size, the RAM left over.
"""
import re
import sys
from pathlib import Path

APP_XIP = 0x02000120
APP_SLOT = 0x8DFBC          # fm1pkg.py APP_SLOT
RAM_ORIGIN, RAM_LEN = 0x01C08000, 0x6C000


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    d = Path(argv[1])
    syms = (d / "kreisel.sym").read_text()

    def sym(name):
        m = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        if not m:
            raise SystemExit(f"fm1_image: symbol {name} not found")
        return int(m.group(1), 16)

    if sym("_start") != APP_XIP:
        raise SystemExit(f"fm1_image: _start is {sym('_start'):#x}, not {APP_XIP:#x}")
    img = bytearray((d / "text.bin").read_bytes())
    parts = [(sym("_rt_load"), (d / "rt.bin").read_bytes()), (sym("_data_load"), (d / "data.bin").read_bytes())]
    for at, blob in sorted(parts):
        if not blob:
            continue
        if at % 4:
            raise SystemExit(f"fm1_image: load address {at:#x} is not word aligned")
        if at - APP_XIP < len(img):
            raise SystemExit("fm1_image: sections overlap")
        img += b"\xff" * (at - APP_XIP - len(img)) + blob
    img += b"\xff" * (-len(img) % 4)
    if len(img) > APP_SLOT:
        raise SystemExit(f"fm1_image: {len(img)} B does not fit the {APP_SLOT} B app slot")
    bss_end = sym("_bss_end")
    left = RAM_ORIGIN + RAM_LEN - bss_end
    if left < 4096:
        raise SystemExit(f"fm1_image: RAM headroom {left} B < 4096 B")
    Path(argv[2]).write_bytes(img)
    print(f"app: {len(img)} B of {APP_SLOT} B slot; RAM .data+.bss {bss_end - RAM_ORIGIN} B of {RAM_LEN} B")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

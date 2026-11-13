#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""THIRD_PARTY_NOTICES.txt for a package that is handed out (the source: Fune's repository, MPL-2.0).

  tools/make_notices.py firmware OUT.txt   the FM-1 and SMK-37 packages (.fwsc): felucca-bsp (MPL-2.0), the JieLi AC79
                                           SDK's files (Apache-2.0), the fonts, the drum samples
  tools/make_notices.py host OUT.txt       the computer, Cardputer Zero, PortMaster and web builds: the fonts, the samples,
                                           SDL2 (zlib) where it is bundled

Every licence's full text is put in (from the files in the repository), with the notices as their authors wrote them."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BSP_SRC = "https://github.com/hugelton/felucca-bsp"
# where Fune's changes to felucca-bsp's files are published (MPL-2.0 section 3.2)
BSP_FUNE_SRC = ("the changes Fune makes to its files are published in felucca-bsp's pull requests "
                  "https://github.com/hugelton/felucca-bsp/pull/38 (chip/usb.c), /pull/39 (boards/fm1/fm1_input.h) and "
                  "/pull/40 (chip/ota.c), and the SMK-37 board in https://github.com/hugelton/felucca-bsp-smk37")

ZLIB = """Simple DirectMedia Layer
Copyright (C) 1997-2024 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held
liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter
it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If
   you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not
   required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original
   software.
3. This notice may not be removed or altered from any source distribution."""


def text(rel):
    return (ROOT / rel).read_text(encoding="utf-8").rstrip() + "\n"


def section(title, body):
    return "=" * 100 + "\n" + title + "\n" + "=" * 100 + "\n\n" + body.rstrip() + "\n\n"


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("firmware", "host"):
        sys.exit(__doc__)
    kind, out = sys.argv[1], Path(sys.argv[2])
    parts = ["THIRD-PARTY NOTICES - Fune\n\nFune itself is under its own licence (LICENSE.txt). It contains the parts below, each "
             "under its own licence; nothing in Fune's licence limits what these licences allow you to do with these parts.\n\n"]
    if kind == "firmware":
        parts.append(section(
            "felucca-bsp: the FM-1's hardware layer (Mozilla Public License 2.0)",
            "Used in the FM-1 and SMK-37 firmware. Its files are under the MPL-2.0, file by file.\n"
            f"Source of felucca-bsp: {BSP_SRC}\n"
            f"Source of the files as Fune changed them: {BSP_FUNE_SRC}\n\n"
            "Its NOTICE:\n\n" + text("third_party/felucca-bsp/NOTICE")))
        parts.append(section(
            "JieLi AC79 AIoT SDK: boot files in the firmware package (Apache License 2.0)",
            "uboot.boot, cfg_tool.bin and eq_cfg_hw.bin from the JieLi AC79 AIoT SDK (JieLi Technology) are put into the "
            "firmware package unchanged. They are under the Apache License 2.0, below."))
    parts.append(section("Inter Tight (SIL Open Font License 1.1)", "Drawn into Fune's screens.\n\n" + text("assets/fonts/OFL-InterTight.txt")))
    parts.append(section("Press Start 2P (SIL Open Font License 1.1)", "Drawn into Fune's tracker and LOFI screens.\n\n" + text("assets/fonts/OFL-PressStart2P.txt")))
    parts.append(section("Fukiai icon font (MIT License)", text("assets/fonts/MIT-Fukiai.txt")))
    parts.append(section("Drum samples (CC0 1.0)", "Three of DRUM's pieces (SP BD, SP CH, SP OH) are made from Virtuosity Drums, dedicated to "
                         "the public domain under CC0 1.0 (https://creativecommons.org/publicdomain/zero/1.0/)."))
    if kind == "host":
        parts.append(section("SDL2 (zlib License): where it is bundled", ZLIB))
    if kind == "firmware":
        parts.append(section("Mozilla Public License 2.0 (full text)", text("LICENSE")))
        parts.append(section("Apache License 2.0 (full text)", text("third_party/felucca-bsp/LICENSES/Apache-2.0.txt")))
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("".join(parts), encoding="utf-8")
    print(f"{out}: {kind}, {out.stat().st_size} bytes")


if __name__ == "__main__":
    main()

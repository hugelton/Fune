#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build Kreisel for the FM-1: build/fm1/kreisel.bin (the app), loader/ota.bin (the update loader), kreisel.fwsc (package).
#
#   platforms/fm1hw/build.sh
#   JIELI_TOOLCHAIN   JieLi Linux toolchain (default ~/.jieli/toolchain)
#   AC79_SDK          JieLi AC79 SDK checkout (default ~/.jieli/ac79-sdk, else ~/fw-AC79_AIoT_SDK); only for the package
#   JIELI_DOCKER      1: run the toolchain in a linux/amd64 container (default on anything but Linux x86-64)
#   KREISEL_FM1_AUDIO=0   no sound in the image
#   KREISEL_FM1_BOOTCHECK=0   no boot-area check (fm1_bootcheck.c): for fm1-emu only, never to hand out
#   KREISEL_FM1_MCPU=r3   hardware float (experimental)
#   KREISEL_BOARD=smk37   the M-VAVE SMK-37 Pro (Alpha): its board (third_party/felucca-bsp/boards/smk37, the
#                         flash map in its smk37_map.h), its target (platforms/targets/smk37.toml), build/smk37/kreisel.fwsc;
#                         KREISEL_SMK_MODEL=elite: the SMK-37 Elite's package (build/smk37_elite). platforms/smk37/build.sh runs it.
#
# Stops after the app and the loader when the SDK is missing. Never flashes anything.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BSP=third_party/felucca-bsp
BOARD="${KREISEL_BOARD:-fm1}"
# the board: its include paths (a board's own headers ahead of the chip's), its target, its package's identity
case "$BOARD" in
fm1)
    OUT=build/fm1
    BINC="-I$BSP/chip -I$BSP/boards/fm1"
    LINC="-I$BSP/chip -I$BSP/boards/fm1"
    PRODUCT=FM-1_1300
    MDEF="" ;;
smk37)
    case "${KREISEL_SMK_MODEL:-pro}" in
    pro) OUT=build/smk37; MODEL="SMK-37 Pro" ;;
    elite) OUT=build/smk37_elite; MODEL="SMK-37 Elite" ;;
    *) echo "build.sh: KREISEL_SMK_MODEL is pro or elite"; exit 1 ;;
    esac
    MAP="$BSP/boards/smk37/smk37_map.h"
    BINC="-I$BSP/boards/smk37 -I$BSP/chip -I$BSP/boards/fm1 -include $MAP"   # (the audio: the FM-1's fm1_audio.h, unchanged)
    LINC="-I$BSP/boards/smk37 -I$BSP/chip -include $MAP -DSMK_LOADER=1"
    PRODUCT="${MODEL}_913"                       # (== FELUCCA_ID in smk37_map.h)
    MDEF="-DSMK_MODEL_NAME=\"$MODEL\"" ;;
*)
    echo "build.sh: KREISEL_BOARD is fm1 or smk37"; exit 1 ;;
esac
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
SDK="${AC79_SDK:-}"
[ -n "$SDK" ] || { for d in "$HOME/.jieli/ac79-sdk" "$HOME/fw-AC79_AIoT_SDK"; do [ -f "$d/cpu/wl82/tools/uboot.boot" ] && SDK="$d" && break; done; }
[ -x "$TC/pi32v2/bin/clang" ] || { echo "build.sh: no JieLi toolchain in $TC (JIELI_TOOLCHAIN)"; exit 1; }
PY="${PYTHON:-python3}"
cd "$ROOT"
mkdir -p "$OUT/gen" "$OUT/obj" "$OUT/loader"

native=0
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] && native=1
if [ "${JIELI_DOCKER:-$((1 - native))}" = 1 ]; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running (colima start / Docker Desktop)"; exit 1; }
    # run TOOL ARGS...: a toolchain binary, paths relative to the repository
    run() { t="$1"; shift; docker run --rm --platform linux/amd64 -v "$ROOT:/work" -v "$TC:/opt/jieli:ro" -w /work \
            "${JIELI_DOCKER_IMAGE:-debian:bookworm-slim}" "/opt/jieli/$t" "$@"; }
    INC=/opt/jieli/pi32v2/include
    LIB=/opt/jieli/pi32v2/lib
else
    run() { t="$1"; shift; "$TC/$t" "$@"; }
    INC="$TC/pi32v2/include"
    LIB="$TC/pi32v2/lib"
fi
cc() { run pi32v2/bin/clang -target pi32v2 "$@"; }

# 1. generated UI data (fonts, icons): the same generators as the CMake build
"$PY" tools/gen_aa_font.py "$OUT/gen/ui_fonts.h" --preset inter-tight >/dev/null
"$PY" tools/gen_vec_font.py "$OUT/gen/ui_vfonts.h" >/dev/null
"$PY" tools/gen_icons.py "$OUT/gen/ui_icons.h" >/dev/null
"$PY" third_party/pendel-dsp/gen_tables.py "$OUT/gen/pd_tables.h" --fs 44100 >/dev/null

# 2. the update loader (the BSP's, unchanged): RAM image -> ota.bin
CFLAGS="-Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function"
cc -c "$BSP/loader/crt0_ldr.S" -o "$OUT/loader/crt0_ldr.o"
cc $CFLAGS $LINC ${MDEF:+"$MDEF"} -Iplatforms/fm1hw -DLDR_BOOT_CHECK=1 -c "$BSP/loader/loader.c" -o "$OUT/loader/loader.o"   # (Fune's boot-area check: fm1_bootcheck.c)
run pi32v2/bin/ld --gc-sections -e _start -T "$BSP/loader/loader.ld" "$OUT/loader/crt0_ldr.o" "$OUT/loader/loader.o" -o "$OUT/loader/loader.elf"
run common/bin/objcopy -O binary -j .text "$OUT/loader/loader.elf" "$OUT/loader/loader.bin"
"$PY" -I tools/fm1_loader_wrap.py "$OUT/loader/loader.bin" "$OUT/loader/ota.bin"

# 3. the app: the portable core, the UI and the audio as they are, plus the FM-1 layer
MCPU=""
[ -n "$KREISEL_FM1_MCPU" ] && MCPU="-mcpu=$KREISEL_FM1_MCPU"
APPFLAGS="$CFLAGS $MCPU -isystem $INC -Isrc -Isrc/ui -Ithird_party/pendel-dsp -I$OUT/gen -DKREISEL_FM1_AUDIO=${KREISEL_FM1_AUDIO:-1} -DKREISEL_FM1_BOOTCHECK=${KREISEL_FM1_BOOTCHECK:-1} $("$PY" -I tools/kbuild.py "$BOARD" defines)"   # (the settings: platforms/targets/fm1.toml, smk37.toml)
OBJS=""
for f in $("$PY" -I tools/kbuild.py "$BOARD" sources); do
    o="$OUT/obj/$(basename "$f" .c).o"
    cc $APPFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
cc -c "$BSP/chip/crt0.S" -o "$OUT/obj/crt0.o"
cc -c "$BSP/chip/fm1_vec.S" -o "$OUT/obj/vec.o"
cc -c "$BSP/chip/fm1_isr.S" -o "$OUT/obj/isr.o"
cc $APPFLAGS $BINC ${MDEF:+"$MDEF"} -c platforms/fm1hw/fm1_main.c -o "$OUT/obj/fm1_main.o"
run pi32v2/bin/ld --gc-sections -T platforms/fm1hw/app.ld "$OUT/obj/crt0.o" "$OUT/obj/vec.o" "$OUT/obj/isr.o" \
    "$OUT/obj/fm1_main.o" $OBJS -L"$LIB" --start-group -lm -lg -lc -lcompiler-rt --end-group -Map "$OUT/kreisel.map" -o "$OUT/kreisel.elf"
run common/bin/objcopy -O binary -j .text "$OUT/kreisel.elf" "$OUT/text.bin"
run common/bin/objcopy -O binary -j .data "$OUT/kreisel.elf" "$OUT/data.bin"
run common/bin/objcopy -O binary -j .ram_text "$OUT/kreisel.elf" "$OUT/rt.bin"
run common/bin/objdump -t "$OUT/kreisel.elf" > "$OUT/kreisel.sym"
# code that runs from RAM while the flash is busy must not call into XIP
if run common/bin/objdump -d -j .ram_text "$OUT/kreisel.elf" | grep -q "call"; then echo "build.sh: .ram_text contains calls"; exit 1; fi
# the CPU erratum (third_party/felucca-bsp/docs/CPU.md): no 64-bit multiply beside a 16-bit load (src/kreisel_mul.h)
run common/bin/objdump -d "$OUT/kreisel.elf" > "$OUT/kreisel.dis"
"$PY" -I tools/fm1_mul_scan.py "$OUT/kreisel.dis" "$OUT/kreisel.sym" || { echo "build.sh: a 64-bit multiply beside a 16-bit load (above)"; exit 1; }
# no FPU: the drawing in integers no soft-float or libm call in its objects
UIOBJS=""; for f in $("$PY" -I tools/kbuild.py --group ui); do UIOBJS="$UIOBJS$(basename "$f" .c).o,"; done   # (the UI's files: draw*.o, gfx.o, ...)
"$PY" -I tools/fm1_float_scan.py "$OUT/kreisel.dis" "$OUT/kreisel.sym" "$OUT/kreisel.map" --objs "${UIOBJS}theme.o" || { echo "build.sh: float in the drawing (above)"; exit 1; }
"$PY" -I tools/fm1_image.py "$OUT" "$OUT/kreisel.bin"

# 4. the package (needs three files of the JieLi SDK, which this repository does not carry)
if [ -z "$SDK" ]; then
    echo "build.sh: no JieLi AC79 SDK (AC79_SDK): app and loader are built, the .fwsc is not"
    exit 0
fi
AC79_SDK="$SDK" "$PY" -I "$BSP/tools/fm1pkg.py" "$OUT/kreisel.bin" "$OUT/loader/ota.bin" "$OUT/kreisel.fwsc" --product "$PRODUCT" --board "$BOARD"
cp LICENSE "$OUT/LICENSE.txt"; cp LICENSING.md "$OUT/"   # (to hand out beside the .fwsc: the MPL and what is under which licence)
"$PY" tools/make_notices.py firmware "$OUT/THIRD_PARTY_NOTICES.txt" >/dev/null
ls -l "$OUT/kreisel.bin" "$OUT/loader/ota.bin" "$OUT/kreisel.fwsc" "$OUT/LICENSE.txt" "$OUT/THIRD_PARTY_NOTICES.txt"

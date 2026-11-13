#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# The Cardputer Zero Alpha to hand out: Fune as the library M5Stack's Cardputer Zero emulator loads, with run.sh and a README
# (the emulator itself not included: a link to its releases). Alpha: tested in the emulator only.
#   bash platforms/czemu/package.sh                 macOS arm64 (this Mac) and Linux x86_64 (an amd64 container under colima)
#   bash platforms/czemu/package.sh macos-arm64     one of them
#   bash platforms/czemu/package.sh linux-x86_64
# -> build/czemu/fune-zero-alpha-<date>-<os>.zip   (the version: src/kreisel_version.h)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
EMU="$ROOT/build_sim/Emulator"
EMU_TAG=v1.0.0; EMU_REV=bd7848a   # the emulator our library is built against (its LVGL 9.5 headers, lv_conf.h)
DATE="$(date +%Y%m%d)"
VERSION="$(sed -n 's/.*KREISEL_VERSION[^"]*"\([^"]*\)".*/\1/p' src/kreisel_version.h | head -1)"
OUT="$ROOT/build/czemu"
OSES="${*:-macos-arm64 linux-x86_64}"
mkdir -p "$OUT" "$ROOT/build_sim"
if [ ! -d "$EMU" ]; then
  git clone --depth 1 --branch "$EMU_TAG" https://github.com/CardputerZero/Emulator.git "$EMU"
  git -C "$EMU" submodule update --init --depth 1
fi
[ "$(git -C "$EMU" rev-parse --short=7 HEAD)" = "$EMU_REV" ] || echo "warning: build_sim/Emulator is not $EMU_TAG ($EMU_REV): the README says $EMU_TAG"

# the library's own symbols must all be in it: the emulator gives only SDL, LVGL and the C library (-undefined dynamic_lookup
# would otherwise leave a missing one to crash when first called)
check_own_symbols() {
  local missing
  missing="$(nm -u "$1" | grep -E '^_?(k_|g_|ui_|sdl_|desk_|kr_|panel_)' || true)"
  [ -z "$missing" ] || { echo "error: $1 leaves these to the emulator:"; echo "$missing"; exit 1; }
}

stage() {   # stage OS LIBFILE EMU_HOWTO EMU_WHERE DATA MIDI
  local os="$1" lib="$2" name="fune-zero-alpha-$DATE-$1"
  local d="$OUT/$name"
  rm -rf "$d" && mkdir -p "$d"
  cp "$lib" "$d/"
  cp platforms/czemu/run.sh "$d/run.sh"
  cp LICENSE "$d/LICENSE.txt"; cp LICENSING.md "$d/"
  python3 tools/make_notices.py host "$d/THIRD_PARTY_NOTICES.txt" >/dev/null
  python3 - "$d/README.txt" "$os" "$name" "$(basename "$lib")" "$3" "$4" "$5" "$6" "$EMU_REV" "$VERSION" "$DATE" <<'PY'
import sys
out, os_, name, lib, howto, where, data, midi, rev, ver, date = sys.argv[1:]
s = open("platforms/czemu/README.in").read()
for k, v in {"OS": os_, "DIR": name, "LIB": lib, "EMU_HOWTO": howto, "EMU_WHERE": where, "DATA": data, "MIDI": midi,
             "EMU_REV": rev, "VERSION": ver, "DATE": date}.items():
    s = s.replace("@" + k + "@", v)
assert "@" not in s.replace("@kurogedelic", ""), "a placeholder left"
open(out, "w").write(s)
PY
  chmod +x "$d/run.sh"
  (cd "$OUT" && rm -f "$name.zip" && COPYFILE_DISABLE=1 zip -qrX "$name.zip" "$name")
  echo "== $OUT/$name.zip"; unzip -l "$OUT/$name.zip"
}

for os in $OSES; do case "$os" in
macos-arm64)
  [ "$(uname -s)-$(uname -m)" = Darwin-arm64 ] || { echo "macos-arm64: on an Apple silicon Mac only"; exit 1; }
  bash platforms/czemu/build.sh --no-run
  check_own_symbols build_sim/libFune.dylib
  stage macos-arm64 build_sim/libFune.dylib \
    "macOS (Apple silicon): the newest release with files, M5CardputerZero-Emulator-macOS-arm64.zip; unzip it and move \"M5CardputerZero Emulator.app\" to Applications. Open it once from Finder (right click, Open) if macOS says it cannot check it." \
    "in /Applications and ~/Applications" \
    "~/Library/Application Support/Kreisel" \
    "CoreMIDI in and out (a virtual source \"Fune\")"
  ;;
linux-x86_64)
  export COLIMA_HOME="${COLIMA_HOME:-$HOME/.colima}"
  docker info >/dev/null 2>&1 || { echo "docker is not running: colima start (COLIMA_HOME=$COLIMA_HOME)"; exit 1; }
  docker build -q --platform linux/amd64 -t fune-czemu:bookworm-amd64 -f platforms/czemu/Dockerfile.linux platforms/czemu >/dev/null
  docker run --rm --platform linux/amd64 -v "$ROOT":/repo -w /repo fune-czemu:bookworm-amd64 bash -c '
    set -eo pipefail
    T=build/czemu/linux-tree
    cmake -S . -B $T -DCMAKE_BUILD_TYPE=Release -DKREISEL_BUILD_SDL=OFF -DKREISEL_BUILD_UI=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
    cmake --build $T -j4 --target kreisel_core kreisel_ui kreisel_store_file 2>&1 | grep -E "error|Error" || true
    E=build_sim/Emulator
    cc -O2 -shared -fPIC -DLV_CONF_INCLUDE_SIMPLE -o build/czemu/libFune.so \
      platforms/czemu/czemu.c platforms/sdl/input.c platforms/sdl/panel.c platforms/sdl/midi.c platforms/sdl/desk.c platforms/sdl/controllers.c \
      platforms/net/kr_rtpmidi.c platforms/net/kr_mdns.c \
      -Iplatforms/net -I$E -I$E/lib -I$E/lib/lvgl -Isrc -Isrc/ui -Iplatforms/sdl -I$T/gen $(sdl2-config --cflags) \
      $T/libkreisel_ui.a $T/libkreisel_store_file.a $T/libkreisel_core.a -lm -Wl,--unresolved-symbols=ignore-all
    if nm -D --undefined-only build/czemu/libFune.so | grep -E " (k_|g_|ui_|sdl_|desk_|kr_|panel_)"; then echo "error: left to the emulator (above)"; exit 1; fi
    strip --strip-unneeded build/czemu/libFune.so
    file build/czemu/libFune.so
    echo "glibc wanted: $(objdump -T build/czemu/libFune.so | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1) (M5Stack built its Linux emulator on Ubuntu 22.04: 2.35)"
  '
  stage linux-x86_64 build/czemu/libFune.so \
    "Linux x86_64: build it from its source at v1.0.0 (its older Linux download cannot load apps). Next to this folder:
     sudo apt install git cmake g++ pkg-config libsdl2-dev libfreetype-dev libpng-dev
     git clone --branch v1.0.0 --recursive https://github.com/CardputerZero/Emulator.git
     cmake -S Emulator -B Emulator/build -DCMAKE_BUILD_TYPE=Release && cmake --build Emulator/build -j4" \
    "in Emulator/build next to this folder" \
    "~/.local/share/kreisel" \
    "RTP-MIDI (network MIDI) on port 5004"
  ;;
*) echo "unknown: $os (macos-arm64, linux-x86_64)"; exit 2 ;;
esac; done

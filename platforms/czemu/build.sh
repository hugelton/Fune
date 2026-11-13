#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Fune in M5Stack's CardputerZero emulator (czemu.c): everything under build_sim/ (git-ignored).
#   bash platforms/czemu/build.sh          # fetch and build the emulator once, build libFune.dylib, run it
#   bash platforms/czemu/build.sh --no-run
# Needs: the desktop build (cmake --build build: the core and UI libraries, the baked fonts), SDL2, git.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/build_sim"
EMU="$SIM/Emulator"
mkdir -p "$SIM"
if [ ! -d "$EMU" ]; then
  git clone --depth 1 https://github.com/CardputerZero/Emulator.git "$EMU"
  git -C "$EMU" submodule update --init --depth 1
fi
if [ ! -x "$EMU/build/cardputer-emu" ]; then
  cmake -S "$EMU" -B "$EMU/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$EMU/build" -j8 >/dev/null
fi
cmake --build "$ROOT/build" --target kreisel_core kreisel_ui kreisel_store_file >/dev/null
EXT=dylib; LINK="-undefined dynamic_lookup -framework CoreMIDI -framework CoreFoundation"; MIDI_EXTRA=""
[ "$(uname)" = Linux ] && EXT=so && LINK="-Wl,--unresolved-symbols=ignore-all" && MIDI_EXTRA="$ROOT/platforms/net/kr_rtpmidi.c $ROOT/platforms/net/kr_mdns.c"
OUT="$SIM/libFune.$EXT"
# shellcheck disable=SC2046
cc -O2 -shared -fPIC -DLV_CONF_INCLUDE_SIMPLE -o "$OUT" \
  "$ROOT/platforms/czemu/czemu.c" "$ROOT/platforms/sdl/input.c" "$ROOT/platforms/sdl/panel.c" "$ROOT/platforms/sdl/midi.c" "$ROOT/platforms/sdl/desk.c" "$ROOT/platforms/sdl/controllers.c" $MIDI_EXTRA \
  -I"$ROOT/platforms/net" -I"$EMU" -I"$EMU/lib" -I"$EMU/lib/lvgl" -I"$ROOT/src" -I"$ROOT/src/ui" -I"$ROOT/platforms/sdl" -I"$ROOT/build/gen" \
  $(sdl2-config --cflags) "$ROOT/build/libkreisel_ui.a" "$ROOT/build/libkreisel_store_file.a" "$ROOT/build/libkreisel_core.a" -lm $LINK   # (SDL: the emulator's own, linked into it: not a second copy)
echo "built $OUT"
[ "${1:-}" = "--no-run" ] && exit 0
cd "$EMU/build" && exec ./cardputer-emu "$OUT"

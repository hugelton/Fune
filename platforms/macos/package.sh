#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Fune for macOS to hand out: Fune.app, universal (Apple silicon and Intel), macOS 11 or later, with SDL2 inside it (the
# official SDL2.framework: Homebrew's SDL2 wants the Mac it was built on), signed ad hoc (not notarised: macOS asks the
# first time, right click and Open), its licences in Contents/Resources.
#   bash platforms/macos/package.sh [SDL2.framework]   ->  build/macos/fune-<version>-macos.zip
# SDL2.framework: from libsdl-org's SDL2 release (SDL2-2.32.10.dmg; default the SDL2.framework of SDL2's macOS release).
set -euo pipefail
cd "$(dirname "$0")/../.."
FW="${1:-${SDL2_FRAMEWORK:-the SDL2.framework of SDL2's macOS release}}"
[ -f "$FW/Resources/CMake/sdl2-config.cmake" ] || { echo "no SDL2.framework at $FW (libsdl-org's SDL2 release, the .dmg)"; exit 1; }
PY="${PYTHON:-python3}"
VER=$(sed -n 's/^#define KREISEL_VERSION_SHORT "\(.*\)".*/\1/p' src/kreisel_version.h)
T=build/macos/tree
cmake -S . -B $T -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
      -DSDL2_DIR="$FW/Resources/CMake" -DPython3_EXECUTABLE="$(command -v "$PY")" \
      "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath,@executable_path/../Frameworks" >/dev/null
cmake --build $T -j8 --target kreisel 2>&1 | grep -E "error|warning:" || true
APP=build/macos/Fune.app
rm -rf "$APP" && cp -R $T/Fune.app "$APP"
mkdir -p "$APP/Contents/Frameworks"
cp -R "$FW" "$APP/Contents/Frameworks/"
cp LICENSE "$APP/Contents/Resources/LICENSE.txt"
cp LICENSING.md "$APP/Contents/Resources/"
"$PY" tools/make_notices.py host "$APP/Contents/Resources/THIRD_PARTY_NOTICES.txt" >/dev/null
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
lipo -archs "$APP/Contents/MacOS/Fune"
otool -L "$APP/Contents/MacOS/Fune" | grep -i sdl
OUT=build/macos/fune-$VER-macos.zip
rm -f "$OUT" && ditto -c -k --keepParent "$APP" "$OUT"
ls -la "$OUT"

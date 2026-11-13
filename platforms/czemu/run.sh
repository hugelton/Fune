#!/bin/sh
# Fune in M5Stack's Cardputer Zero emulator (Alpha). Starts the emulator with Fune as its app.
#   ./run.sh                       finds the emulator (macOS: /Applications or ~/Applications; else Emulator/build next to this folder)
#   CZEMU=/path/to/emulator ./run.sh   (the .app, or the folder or file cardputer-emu)
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$HERE/libFune.dylib"; [ "$(uname)" = Linux ] && LIB="$HERE/libFune.so"
find_emu() {
  for p in "$CZEMU" "$CZEMU/Contents/MacOS/cardputer-emu" "$CZEMU/cardputer-emu" \
           "/Applications/M5CardputerZero Emulator.app/Contents/MacOS/cardputer-emu" \
           "$HOME/Applications/M5CardputerZero Emulator.app/Contents/MacOS/cardputer-emu" \
           "$HERE/../Emulator/build/cardputer-emu" "$HERE/Emulator/build/cardputer-emu" \
           "$HERE/../M5CardputerZero-Emulator/cardputer-emu" "$HERE/M5CardputerZero-Emulator/cardputer-emu"; do
    [ -n "$p" ] && [ -f "$p" ] && [ -x "$p" ] && { echo "$p"; return 0; }
  done
  return 1
}
EMU="$(find_emu)" || { echo "M5Stack's Cardputer Zero emulator not found: see README.txt (or CZEMU=/path/to/it ./run.sh)"; exit 1; }
[ "$(uname)" = Darwin ] && xattr -d com.apple.quarantine "$LIB" 2>/dev/null   # (a downloaded zip: macOS may refuse the library)
exec "$EMU" "$LIB"

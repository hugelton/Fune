#!/bin/bash
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Fune for the M5Stack Cardputer Zero's Linux (arm64, SDL2 + ALSA), an Alpha: built in the PortMaster build's arm64 Debian
# container (platforms/portmaster/Dockerfile) under colima on the SSD. Not yet tried on a device.
#   bash platforms/zero/build.sh   ->  build/zero/fune-zero-alpha-<date>-linux-arm64.tar.gz
# On the Zero: tar xzf it, ./install.sh, then fune (platforms/zero/README.txt).
set -euo pipefail
cd "$(dirname "$0")/../.."
export COLIMA_HOME="${COLIMA_HOME:-$HOME/.colima}"
IMG=fune-portmaster:bookworm   # (the same machine as the PortMaster build: Debian 12, glibc 2.36, as Raspberry Pi OS 12)
docker info >/dev/null 2>&1 || { echo "docker is not running: colima start (COLIMA_HOME=$COLIMA_HOME)"; exit 1; }
docker build -q --platform linux/arm64 -t $IMG platforms/portmaster >/dev/null
docker run --rm --platform linux/arm64 -v "$PWD":/repo -w /repo $IMG bash -c '
  set -eo pipefail
  cmake -S . -B build/zero/tree -DCMAKE_BUILD_TYPE=Release -DKREISEL_BUILD_SDL=ON >/dev/null
  cmake --build build/zero/tree -j4 --target kreisel kreisel_plugin_fm2 kreisel_plugin_flat 2>&1 | grep -E "error|Error|Built target kreisel$"
  strip build/zero/tree/kreisel
  file build/zero/tree/kreisel
  echo "glibc wanted: $(objdump -T build/zero/tree/kreisel | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)"
  echo "needs: $(objdump -p build/zero/tree/kreisel | awk "/NEEDED/{print \$2}" | tr "\n" " ")"
  grep -q snd_seq_open build/zero/tree/kreisel && echo "MIDI: the ALSA sequencer" || { echo "no ALSA MIDI in the build"; exit 1; }
'
NAME="fune-zero-alpha-$(date +%Y%m%d)-linux-arm64"
P=build/zero/$NAME
rm -rf "$P" && mkdir -p "$P/bin" "$P/plugins"
cp build/zero/tree/kreisel "$P/bin/kreisel"
cp build/zero/tree/plugins/*.so "$P/plugins/"
cp platforms/zero/fune platforms/zero/install.sh "$P/"
cp platforms/zero/README.txt "$P/README.txt"
cp LICENSE "$P/LICENSE.txt"; cp LICENSING.md "$P/"
python3 tools/make_notices.py host "$P/THIRD_PARTY_NOTICES.txt" >/dev/null
chmod +x "$P/fune" "$P/install.sh" "$P/bin/kreisel"
(cd build/zero && rm -f "$NAME.tar.gz" && COPYFILE_DISABLE=1 tar --uid 0 --gid 0 -czf "$NAME.tar.gz" "$NAME")
(cd build/zero && tar tzvf "$NAME.tar.gz")
echo "archive: build/zero/$NAME.tar.gz"

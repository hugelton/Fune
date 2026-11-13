#!/bin/bash
# A PortMaster port of Fune (arm64 Linux, SDL2), built in an arm64 container under colima (on the SSD):
#   bash platforms/portmaster/build.sh        ->  build/portmaster/port/  (Fune.sh and fune/: copy both to ports/)
# The screen is the profile h700 (320 x 240 at 2 x on its 640 x 480, a pad: platforms/targets/h700.toml).
set -euo pipefail
cd "$(dirname "$0")/../.."
export COLIMA_HOME="${COLIMA_HOME:-$HOME/.colima}"
IMG=fune-portmaster:bookworm
docker info >/dev/null 2>&1 || { echo "docker is not running: colima start (COLIMA_HOME=$COLIMA_HOME)"; exit 1; }
docker build -q --platform linux/arm64 -t $IMG platforms/portmaster
docker run --rm --platform linux/arm64 -v "$PWD":/repo -w /repo $IMG bash -c '
  set -eo pipefail
  cmake -S . -B build/portmaster/tree -DCMAKE_BUILD_TYPE=Release -DKREISEL_BUILD_SDL=ON >/dev/null
  cmake --build build/portmaster/tree -j4 --target kreisel kreisel_plugin_fm2 kreisel_plugin_flat 2>&1 | grep -E "error|Error|Built target kreisel$"
  strip build/portmaster/tree/kreisel
  file build/portmaster/tree/kreisel
  echo "glibc wanted: $(objdump -T build/portmaster/tree/kreisel 2>/dev/null | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)"
'
P=build/portmaster/port
rm -rf $P && mkdir -p $P/fune/conf/kreisel/plugins
cp platforms/portmaster/Fune.sh $P/
cp build/portmaster/tree/kreisel $P/fune/fune.aarch64
cp build/portmaster/tree/plugins/*.so $P/fune/conf/kreisel/plugins/
cp platforms/portmaster/README.port.md $P/fune/README.md
cp LICENSE $P/fune/LICENSE.txt; cp LICENSING.md $P/fune/
python3 tools/make_notices.py host $P/fune/THIRD_PARTY_NOTICES.txt >/dev/null
chmod +x $P/Fune.sh $P/fune/fune.aarch64
(cd build/portmaster && rm -f fune-portmaster.zip && zip -qr fune-portmaster.zip port)
ls -la $P $P/fune
echo "zip: build/portmaster/fune-portmaster.zip"

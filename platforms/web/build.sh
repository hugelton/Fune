#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Builds the browser module into platforms/web/dist/kreisel.js (WebAssembly inside it). Needs emcc and the baked fonts
# (build/gen, from the native CMake build: cmake --build build --target kreisel_fonts). What it builds: platforms/targets/web.toml, web_audio.toml.
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/ui_fonts.h ] && [ -f build/gen/pd_tables.h ] || { echo "no build/gen/ui_fonts.h or pd_tables.h: run cmake -S . -B build && cmake --build build --target kreisel_fonts kreisel_fx_tables" >&2; exit 1; }
mkdir -p platforms/web/dist
# what it builds (platforms/targets/): taken first, so a failure (no Python 3.11 tomllib) stops here, said
WEB_SRC=$(python3 -I tools/kbuild.py web sources); WEB_DEF=$(python3 -I tools/kbuild.py web defines)
AUD_SRC=$(python3 -I tools/kbuild.py web_audio sources); AUD_DEF=$(python3 -I tools/kbuild.py web_audio defines)
EXPORTS=$(grep -o 'EMSCRIPTEN_KEEPALIVE [a-z0-9_ *]*kw_[a-z0-9_]*' platforms/web/kreisel_web.c | sed 's/.* \**//' | sed 's/^/_/' | paste -sd, -),_malloc,_free   # (malloc: the page hands bytes in, plugins.html)
emcc -O2 -std=gnu11 -Isrc -Isrc/ui -Ibuild/gen -Ithird_party/pendel-dsp \
  $WEB_DEF $WEB_SRC \
  -sMODULARIZE=1 -sEXPORT_NAME=KreiselModule -sSINGLE_FILE=1 -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH=1 \
  -sEXPORTED_FUNCTIONS="$EXPORTS" -sEXPORTED_RUNTIME_METHODS=cwrap,UTF8ToString,HEAPU8 \
  -o platforms/web/dist/kreisel.js
echo "built platforms/web/dist/kreisel.js"
# the sound: the audio host alone, as a standalone WebAssembly module (no glue, no threads) for the AudioWorklet
emcc -O2 -std=gnu11 -Isrc -Ibuild/gen -Ithird_party/pendel-dsp $AUD_DEF $AUD_SRC \
  -sSTANDALONE_WASM=1 --no-entry -sALLOW_MEMORY_GROWTH=0 -sINITIAL_MEMORY=4194304 -sSTACK_SIZE=262144 -sERROR_ON_UNDEFINED_SYMBOLS=0  \
  -o platforms/web/dist/kreisel_audio.wasm
python3 - <<'PY'
import base64, json
w = open('platforms/web/dist/kreisel_audio.wasm', 'rb').read()
open('platforms/web/dist/kreisel_audio.js', 'w').write('window.KREISEL_AUDIO_WASM = "' + base64.b64encode(w).decode() + '";\nwindow.KREISEL_WORKLET = ' + json.dumps(open('platforms/web/worklet.js').read()) + ';\n')
PY
echo "built platforms/web/dist/kreisel_audio.wasm (+ .js: the base64 + worklet.js source, for the page)"

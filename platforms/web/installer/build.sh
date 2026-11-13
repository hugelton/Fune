#!/bin/bash
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# The web installer's folder, ready for a static host (hugelton.com/<slug>/, platforms/web/installer/README.md):
#   bash platforms/web/installer/build.sh [OUT]     (OUT: build/installer)
# Takes the packages already built: build/fm1/kreisel.fwsc (platforms/fm1hw/build.sh), build/smk37/kreisel.fwsc and
# build/smk37_elite/kreisel.fwsc (platforms/smk37/build.sh) where they exist. Writes packages.json (each package's identity,
# size and SHA-256, which the page checks after the download), the licence and the firmware's notices.
set -euo pipefail
cd "$(dirname "$0")/../../.."
OUT="${1:-build/installer}"
rm -rf "$OUT" && mkdir -p "$OUT"
cp platforms/web/installer/index.html platforms/web/installer/installer.js "$OUT/"
cp platforms/web/icon-192.png "$OUT/"
cp LICENSE "$OUT/LICENSE.txt"
python3 tools/make_notices.py firmware "$OUT/THIRD_PARTY_NOTICES.txt" >/dev/null
python3 - "$OUT" <<'EOF'
import hashlib, json, os, shutil, sys
sys.path.insert(0, "third_party/felucca-bsp/tools")
import fm1_install as f
out = sys.argv[1]
SRC = [("build/fm1/kreisel.fwsc", "fune-fm1.fwsc", "M-VAVE FM-1", False),
       ("build/smk37/kreisel.fwsc", "fune-smk37-pro.fwsc", "M-VAVE SMK-37 Pro", True),
       ("build/smk37_elite/kreisel.fwsc", "fune-smk37-elite.fwsc", "M-VAVE SMK-37 Elite", True)]
lst = []
for src, name, device, alpha in SRC:
    if not os.path.exists(src):
        continue
    raw = open(src, "rb").read()
    product = f.product_of(raw)
    if f.LOADER_MARK not in raw:
        sys.exit(f"{src}: no update loader in it")
    shutil.copyfile(src, os.path.join(out, name))
    lst.append({"product": product, "device": device, "file": name, "size": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(), "alpha": alpha})
    print(f"{name}: {product}, {len(raw)} B{' (Alpha)' if alpha else ''}")
if not lst:
    sys.exit("no packages built: platforms/fm1hw/build.sh first")
json.dump(lst, open(os.path.join(out, "packages.json"), "w"), indent=1)
EOF
echo "installer: $OUT (try: python3 platforms/web/serve.py 8124 from $OUT's parent, then /$(basename "$OUT")/)"

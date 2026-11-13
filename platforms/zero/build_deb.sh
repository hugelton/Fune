#!/bin/bash
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Fune for the Cardputer Zero as a .deb in M5Stack's APPLaunch layout (CardputerZero-AppBuilder's conventions, 2026-11-13):
#   /usr/share/APPLaunch/apps/fune/        the program and its plugins
#   /usr/share/APPLaunch/bin/fune          the start script (Wayland, else KMSDRM: platforms/zero/fune)
#   /usr/share/APPLaunch/applications/fune.desktop, share/images/fune.png (48 px)   the launcher's entry and icon
# No systemd service: it is an app the launcher starts (as the user pi), not a daemon. Built from our own binary only
# (closed source), in the same arm64 Debian container as platforms/zero/build.sh.
#   bash platforms/zero/build_deb.sh    ->  build/zero/fune_<version>_arm64.deb
# On the Zero: sudo apt install ./fune_<version>_arm64.deb   (remove: sudo apt remove fune; songs and presets stay)
set -euo pipefail
cd "$(dirname "$0")/../.."
bash platforms/zero/build.sh >/dev/null   # (build/zero/tree: the program and plugins)
VER=$(sed -n 's/^#define KREISEL_VERSION_SHORT "\(.*\)".*/\1/p' src/kreisel_version.h)
LABEL=$(sed -n 's/^#define KREISEL_VERSION "\(.*\)".*/\1/p' src/kreisel_version.h)
case "$LABEL" in *Alpha*) DEBVER="$VER~alpha" ;; *Beta*) DEBVER="$VER~beta" ;; *) DEBVER="$VER" ;; esac   # (~: before the release)
ST=build/zero/deb
rm -rf "$ST" && mkdir -p "$ST/DEBIAN" "$ST/usr/share/APPLaunch/apps/fune/bin" "$ST/usr/share/APPLaunch/apps/fune/plugins" \
  "$ST/usr/share/APPLaunch/bin" "$ST/usr/share/APPLaunch/applications" "$ST/usr/share/APPLaunch/share/images" "$ST/usr/share/doc/fune"
A=$ST/usr/share/APPLaunch/apps/fune
cp build/zero/tree/kreisel "$A/bin/kreisel"
cp build/zero/tree/plugins/*.so "$A/plugins/"
cp platforms/zero/fune "$A/fune"
cp platforms/zero/README.txt LICENSE LICENSING.md "$ST/usr/share/doc/fune/"
python3 tools/make_notices.py host "$ST/usr/share/doc/fune/THIRD_PARTY_NOTICES.txt" >/dev/null
python3 -c "from PIL import Image; Image.open('assets/app/kreisel.png').resize((48, 48), Image.LANCZOS).save('$ST/usr/share/APPLaunch/share/images/fune.png')"
cat >"$ST/usr/share/APPLaunch/bin/fune" <<'EOF'
#!/bin/sh
# APPLaunch's entry: the app's own start script (the display, the log in ~/.local/share/kreisel/log.txt)
exec /usr/share/APPLaunch/apps/fune/fune "$@"
EOF
printf '[Desktop Entry]\nName=Fune\nExec=/usr/share/APPLaunch/bin/fune\nTerminal=false\nIcon=share/images/fune.png\nType=Application\n' \
  >"$ST/usr/share/APPLaunch/applications/fune.desktop"
cat >"$ST/DEBIAN/control" <<EOF
Package: fune
Version: $DEBVER
Architecture: arm64
Maintainer: HUGELTON INSTRUMENTS <info@hugelton.com>
Section: APPLaunch
Priority: optional
Depends: libsdl2-2.0-0, libasound2
Homepage: https://hugelton.com
Description: Fune $LABEL: a groovebox
 Four tracks of synths and drums, a step sequencer and effects, for the
 M5Stack Cardputer Zero. Alpha: not yet tried on a device.
EOF
chmod 0755 "$A/bin/kreisel" "$A/fune" "$ST/usr/share/APPLaunch/bin/fune"
chmod 0644 "$A/plugins/"*.so
OUT="build/zero/fune_${DEBVER}_arm64.deb"
docker run --rm --platform linux/arm64 -v "$PWD":/repo -w /repo argosy-portmaster:bookworm \
  sh -c "chown -R root:root $ST && dpkg-deb --build --root-owner-group $ST $OUT >/dev/null && dpkg-deb -I $OUT && dpkg-deb -c $OUT && chown -R $(id -u):$(id -g) $ST $OUT"
echo "deb: $OUT"

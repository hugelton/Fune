#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Installs Fune (Cardputer Zero Alpha) for this user: the program in ~/.local/share/kreisel/app, its plugins in
# ~/.local/share/kreisel/plugins, the command ~/.local/bin/fune. Songs and presets already there are kept.
#   ./install.sh              install (or update)
#   ./install.sh --launcher   also an entry in M5Stack's launcher (APPLaunch; asks for sudo)
#   ./install.sh --remove     remove the program and the command (songs and presets stay)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/kreisel"
APP="$DATA/app"
case "${1:-}" in
  --remove)
    rm -rf "$APP" "$HOME/.local/bin/fune"
    [ -f /usr/share/APPLaunch/applications/fune.desktop ] && sudo rm -f /usr/share/APPLaunch/applications/fune.desktop
    echo "removed; songs and presets stay in $DATA"; exit 0 ;;
esac
mkdir -p "$APP/bin" "$DATA/plugins" "$HOME/.local/bin"
cp "$HERE/bin/kreisel" "$APP/bin/kreisel"
cp "$HERE/fune" "$APP/fune"
cp "$HERE/plugins/"*.so "$DATA/plugins/"
chmod +x "$APP/bin/kreisel" "$APP/fune"
ln -sf "$APP/fune" "$HOME/.local/bin/fune"
echo "installed: $APP (start: fune, or $APP/fune)"
if [ "${1:-}" = "--launcher" ]; then
  printf '[Desktop Entry]\nName=Fune\nExec=%s\nTerminal=false\nType=Application\n' "$APP/fune" >"$DATA/fune.desktop"
  sudo install -D -m 0644 "$DATA/fune.desktop" /usr/share/APPLaunch/applications/fune.desktop
  echo "launcher entry: /usr/share/APPLaunch/applications/fune.desktop"
fi

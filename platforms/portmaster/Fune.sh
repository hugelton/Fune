#!/bin/bash
# PORTMASTER: fune.zip, Fune.sh
# Fune, a groovebox. A test build: the pad moves the cursor and turns what it is on (the Switch's controls).
XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}
if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi
source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/fune"
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export XDG_DATA_HOME="$GAMEDIR/conf"          # songs, presets, plugins: fune/conf/kreisel
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

$GPTOKEYB "fune.${DEVICE_ARCH}" &           # (only the exit: Select + Start)
pm_platform_helper "$GAMEDIR/fune.${DEVICE_ARCH}"
./fune.${DEVICE_ARCH} --profile h700

pm_finish

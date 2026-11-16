#!/bin/bash

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
export PORT_32BIT="Y"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/thug"
cd $GAMEDIR
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

# First run: extract the disc image in gamedata/ into that folder.
if [ ! -f "$GAMEDIR/gamedata/data/pre/qb.prx" ]; then
  export THUG_ISO=$(find "$GAMEDIR/gamedata" -maxdepth 1 -type f -iname '*.iso' | head -n 1)
  if [ -z "$THUG_ISO" ]; then
    pm_message "Copy your USA Xbox disc image (.iso) into ports/thug/gamedata (see the README)."
    sleep 5
    pm_finish
    exit 1
  fi
  export GAMEDIR
  export PATCHER_FILE="$GAMEDIR/tools/patchscript"
  export PATCHER_GAME="Tony Hawk's Underground"
  export PATCHER_TIME="about 15 minutes"
  $ESUDO chmod a+x "$GAMEDIR/tools/patchscript"
  source "$controlfolder/utils/patcher.txt"
  [ -f "$GAMEDIR/gamedata/data/pre/qb.prx" ] || { pm_finish; exit 1; }
fi

# Optional overrides, one VAR=value per line (see the README)
[ -f "$GAMEDIR/thug.cfg" ] && { set -a; source "$GAMEDIR/thug.cfg"; set +a; }

export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
chmod +x "$GAMEDIR/thug.armhf"

$GPTOKEYB "thug.armhf" &
pm_platform_helper "$GAMEDIR/thug.armhf"
./thug.armhf

pm_finish

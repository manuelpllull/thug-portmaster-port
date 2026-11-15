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

if [ ! -f "$GAMEDIR/gamedata/data/pre/qb.prx" ]; then
  pm_message "Copy your extracted USA Xbox disc into ports/thug/gamedata (see the README)."
  sleep 5
  pm_finish
  exit 1
fi

# Optional overrides, one VAR=value per line (see the README)
[ -f "$GAMEDIR/thug.cfg" ] && { set -a; source "$GAMEDIR/thug.cfg"; set +a; }

export LD_LIBRARY_PATH="$GAMEDIR/libs.armhf:$LD_LIBRARY_PATH"
# R36S pad by button position (B = Cross, A = Circle, Y = Square, X = Triangle),
# after PortMaster's mapping so it only replaces that pad's entry.
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig
190000004b4800000011000000010000,GO-Super Gamepad,a:b0,b:b1,x:b3,y:b2,back:b12,start:b13,leftstick:b14,rightstick:b15,guide:b16,dpleft:b10,dpdown:b9,dpright:b11,dpup:b8,leftshoulder:b4,lefttrigger:b6,rightshoulder:b5,righttrigger:b7,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux,"
chmod +x "$GAMEDIR/thug.armhf"

$GPTOKEYB "thug.armhf" &
pm_platform_helper "$GAMEDIR/thug.armhf"
./thug.armhf

pm_finish

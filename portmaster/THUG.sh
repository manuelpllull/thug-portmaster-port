#!/bin/bash
# PortMaster launcher for Tony Hawk's Underground (THUG)
# Built as a 32-bit (armhf) native game on SDL2 + GLES2 / OpenGL.
#
# Follows PortMaster standards for RK3326 / R36S handhelds.

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

PORTMASTER=0
if [ -f "$controlfolder/control.txt" ]; then
  source "$controlfolder/control.txt"
  export PORT_32BIT="Y"
  [ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
  get_controls
  PORTMASTER=1
fi

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
if [ -n "${directory:-}" ] && [ -d "/$directory/ports/thug" ]; then
  GAMEDIR="/$directory/ports/thug"
else
  GAMEDIR="$SCRIPT_DIR/thug"
fi
cd "$GAMEDIR" || exit 1

# Setup logging
> "$GAMEDIR/log.txt"
if [ -n "${BASH_VERSION:-}" ]; then
  eval 'exec > >(tee "$GAMEDIR/log.txt") 2>&1'
else
  exec >> "$GAMEDIR/log.txt" 2>&1
fi

CURR_TTY="${CUR_TTY:-/dev/tty1}"

SUDO="sudo -n"
[ "$(id -u)" = 0 ] && SUDO=""
CLOCK_STATE=""

finish() {
  if [ "$PORTMASTER" = 1 ]; then
    pm_finish
  fi
  restore_clocks 2>/dev/null
  printf '\033c' 2>/dev/null > "$CURR_TTY"
}

cannot_start() {
  echo "$1"
  {
    printf '\033c'
    printf ' Tony Hawk\x27s Underground cannot start\n\n'
    printf '%s\n' "$1"
    printf '\n (log.txt in ports/thug has details)\n'
  } 2>/dev/null > "$CURR_TTY"
  if [ "$PORTMASTER" = 1 ] && command -v pm_show_error > /dev/null 2>&1; then
    pm_show_error "Tony Hawk's Underground cannot start.

$1" --timeout 30
  else
    sleep 10
  fi
  finish
  exit 1
}

echo "[launcher] $(date) gamedir=$GAMEDIR portmaster=$PORTMASTER cfw=${CFW_NAME:-unknown}"

# ----------------------------------------------------------------- Clocks
lock_clocks() {
  if [ "${THUG_CLOCKS:-1}" = "0" ]; then return 0; fi
  for gov in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor \
             /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor \
             /sys/class/devfreq/*/governor; do
    [ -f "$gov" ] || continue
    old=$(cat "$gov" 2>/dev/null)
    echo performance | $SUDO tee "$gov" > /dev/null 2>&1 && CLOCK_STATE="$CLOCK_STATE $gov=$old"
  done
}

restore_clocks() {
  for entry in $CLOCK_STATE; do
    echo "${entry#*=}" | $SUDO tee "${entry%%=*}" > /dev/null 2>&1
  done
  if [ "${SWAP_ADDED:-0}" = 1 ]; then
    $SUDO swapoff "$GAMEDIR/thug.swap" > /dev/null 2>&1
  fi
}
trap restore_clocks EXIT

# ----------------------------------------------------------------- Swapfile
ensure_swap() {
  local swap_total
  swap_total=$(awk '/SwapTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)
  if [ "$swap_total" -lt 262144 ]; then
    echo "[launcher] Low swap detected ($swap_total kB). Checking safety swapfile..."
    if [ ! -f "$GAMEDIR/thug.swap" ]; then
      echo "[launcher] Creating 512 MB swapfile in $GAMEDIR/thug.swap"
      dd if=/dev/zero of="$GAMEDIR/thug.swap" bs=1M count=512 2>/dev/null || true
      chmod 600 "$GAMEDIR/thug.swap" 2>/dev/null || true
      mkswap "$GAMEDIR/thug.swap" >/dev/null 2>&1 || true
    fi
    if [ -f "$GAMEDIR/thug.swap" ] && $SUDO swapon "$GAMEDIR/thug.swap" 2>/dev/null; then
      SWAP_ADDED=1
      echo "[launcher] Activated safety swapfile"
    fi
  fi
}

# ----------------------------------------------------------------- Install
console_install() {
  local pct=0 line
  if [ -w "$CURR_TTY" ] && command -v dialog > /dev/null 2>&1; then
    "$GAMEDIR/tools/patchscript" | while trap '' PIPE; IFS= read -r line; do
      echo "$line" >&2
      case "$line" in
        PATCH_FAIL_MSG:*|"Patching process failed!") continue ;;
        \[*%\]*) pct=$(echo "$line" | sed 's/^\[ *\([0-9]*\)%\].*/\1/') ;;
      esac
      printf '%s\n' "$pct"
    done | dialog --title "Tony Hawk's Underground" --gauge "Extracting game data from disc image..." 8 60 0 > "$CURR_TTY" 2>&1
  else
    "$GAMEDIR/tools/patchscript"
  fi
}

run_install() {
  local iso
  iso=$(find "$GAMEDIR/gamedata" "$GAMEDIR" -maxdepth 2 -iname "*.iso" 2>/dev/null | head -n 1)
  if [ -z "$iso" ]; then
    cannot_start "Missing game data and no ISO found.
Please copy Tony Hawk's Underground (USA) Xbox ISO to:
$GAMEDIR/gamedata/"
  fi
  export THUG_ISO="$iso"
  echo "[launcher] Running install from $iso"
  if [ "$PORTMASTER" = 1 ] && [ -f "$controlfolder/utils/patcher.txt" ]; then
    export PATCHER_FILE="$GAMEDIR/tools/patchscript"
    export PATCHER_GAME="Tony Hawk's Underground"
    export PATCHER_TIME="8 to 10 minutes"
    source "$controlfolder/utils/patcher.txt"
  else
    console_install
  fi
}

# ----------------------------------------------------------------- Verification
# Check for extracted game data (Data/scripts/qdir.txt)
THUG_DATA_DIR=""
for d in "$GAMEDIR/Data" "$GAMEDIR/data" "$GAMEDIR/gamedata/Data" "$GAMEDIR/gamedata/data"; do
  if [ -f "$d/scripts/qdir.txt" ]; then
    THUG_DATA_DIR="$d"
    break
  fi
done

if [ -z "$THUG_DATA_DIR" ]; then
  iso=$(find "$GAMEDIR/gamedata" "$GAMEDIR" -maxdepth 2 -iname "*.iso" 2>/dev/null | head -n 1)
  if [ -n "$iso" ]; then
    echo "[launcher] Running installer for $iso..."
    run_install
    for d in "$GAMEDIR/Data" "$GAMEDIR/data" "$GAMEDIR/gamedata/Data" "$GAMEDIR/gamedata/data"; do
      if [ -f "$d/scripts/qdir.txt" ]; then
        THUG_DATA_DIR="$d"
        break
      fi
    done
  fi
fi

if [ -z "$THUG_DATA_DIR" ]; then
  if [ -f "$GAMEDIR/gamedata/SKATE5.WAD" ] || [ -f "$GAMEDIR/gamedata/SLUS_207.31" ]; then
    cannot_start "PS2 files detected (SKATE5.WAD / SLUS_207.31).
This ARM port requires Tony Hawk's Underground Xbox (USA) assets.
Please place the Xbox USA Data/ folder or Xbox ISO in:
$GAMEDIR/Data/ or $GAMEDIR/gamedata/"
  else
    cannot_start "Missing game data!
Please copy Tony Hawk's Underground (USA) Xbox Data/ folder
or Xbox ISO to:
$GAMEDIR/Data/ or $GAMEDIR/gamedata/"
  fi
fi

export THUG_DATA_DIR
echo "[launcher] THUG_DATA_DIR=$THUG_DATA_DIR"

# ----------------------------------------------------------------- Gamepad
# Gamepad configuration for R36S and PortMaster
[ -n "${sdl_controllerconfig:-}" ] && export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
R36S_PAD_GUID="190000004b4800000011000000010000"
R36S_PAD_MAPPING='190000004b4800000011000000010000,GO-Super Gamepad,a:b0,b:b1,x:b3,y:b2,back:b12,start:b13,leftstick:b14,rightstick:b15,guide:b16,dpleft:b10,dpdown:b9,dpright:b11,dpup:b8,leftshoulder:b4,lefttrigger:b6,rightshoulder:b5,righttrigger:b7,leftx:a0,lefty:a1,rightx:a2,righty:a3,platform:Linux,'
if ! printf '%s\n' "${SDL_GAMECONTROLLERCONFIG:-}" | grep -F "$R36S_PAD_GUID" | grep -q 'leftstick:'; then
  if [ -n "${SDL_GAMECONTROLLERCONFIG:-}" ]; then
    export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG}
$R36S_PAD_MAPPING"
  else
    export SDL_GAMECONTROLLERCONFIG="$R36S_PAD_MAPPING"
  fi
fi

# Setup config overrides if config file exists
if [ -f "$GAMEDIR/thug.cfg" ]; then
  source "$GAMEDIR/thug.cfg"
fi

lock_clocks
ensure_swap

echo "[launcher] Starting thug.armhf..."
printf '\033c' 2>/dev/null > "$CURR_TTY"

if [ "$PORTMASTER" = 1 ]; then
  [ -n "$GPTOKEYB" ] && $GPTOKEYB "thug.armhf" &
  pm_platform_helper "$GAMEDIR/thug.armhf"
fi

"$GAMEDIR/thug.armhf"
EXIT_CODE=$?

echo "[launcher] Game exited with code $EXIT_CODE"
if [ "$EXIT_CODE" != 0 ]; then
  echo "[launcher] memory after the game:"
  grep -E "^(MemTotal|MemAvailable|SwapTotal|SwapFree):" /proc/meminfo 2>/dev/null
  echo "[launcher] the kernel's last lines:"
  { $SUDO dmesg 2>/dev/null || dmesg 2>/dev/null; } | tail -n 60
fi

finish
exit $EXIT_CODE

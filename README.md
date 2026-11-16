# THUG for PortMaster

*Tony Hawk's Underground* (2003), running natively on Linux handhelds through
[PortMaster](https://portmaster.games).

This isn't an emulator: it's the game's own code, built for 32-bit ARM (armhf)
and drawn with OpenGL ES through [gl4es](https://github.com/ptitSeb/gl4es).
It grew out of my PS Vita port of THUG, and runs on the same engine backends.

You need your own copy of the game. This repository and its releases don't
include any game data.

## Devices

| Device | Screen | CFW | Status |
|---|---|---|---|
| R36S (RK3326) | 640x480 | – | Runs. Frame-time hitches remain in busy areas |
| R36S clone (RK3326) | 640x480 | dArkOS | Runs (community report, European disc) |
| RG34XX (H700) | 720x480 | Knulli | Runs. Busy scenes drop frames; `THUG_RENDER_SCALE=0.75` helps |

Other PortMaster devices with an armhf userland and 1 GB of RAM should work but
haven't been tested. Reports are welcome in the [issues](../../issues).

## What you need

- A handheld with [PortMaster](https://portmaster.games) installed.
- The **Xbox** version of *Tony Hawk's Underground* as a disc image (`.iso`)
  dumped from a disc you own. The USA release is tested; the European one has
  been reported working. PS2 and GameCube versions won't work: the game reads
  the Xbox data files.
- About **3.5 GB** free for the extracted game, plus room for the `.iso`
  during the first launch.

## Installing

1. Copy `thug.zip` to PortMaster's `autoinstall` folder and open PortMaster.
   It installs `THUG.sh` and the `thug` folder into `ports`.
2. Copy your disc image (`.iso`) into `ports/thug/gamedata`.
3. Start **Tony Hawk's Underground** from Ports. The first launch extracts the
   disc image (several minutes; it needs `python3` on the device). Afterwards
   you can delete the `.iso`.

No `python3` on your firmware? Extract the disc on a computer with
[extract-xiso](https://github.com/XboxDev/extract-xiso)
(`extract-xiso -x "Tony Hawk's Underground (USA).iso"`) and copy the `data`
folder it produces into `ports/thug/gamedata`, so that
`ports/thug/gamedata/data/pre/qb.prx` exists.

The first load of each level takes longer while shaders are compiled; they're
cached for the next time.

## Controls

| Button | What it does |
|---|---|
| Left stick / D-pad | Skate, menus |
| B | Ollie, accept |
| Y | Flip trick |
| A | Grab trick, back |
| X | Grind, lip trick |
| L1 / R1 | Spin left / right |
| L2 / R2 | Nollie / switch stance, reverts |
| Select | Get off the board |
| Start | Pause |
| R3 + Select | FPS meter on/off |
| Select + Start | Quit |

Face buttons follow their printed labels on every device, since PortMaster maps
pads by label.

## Settings

Optional settings go in `ports/thug/thug.cfg`, one `VAR=value` per line:

| Setting | What it does |
|---|---|
| `THUG_BTN_OLLIE`, `THUG_BTN_GRAB`, `THUG_BTN_FLIP`, `THUG_BTN_GRIND` | Move an action to another face button: `a`, `b`, `x` or `y`. For example `THUG_BTN_OLLIE=a` and `THUG_BTN_GRAB=b` swap those two |
| `THUG_FPS_METER=1` | Show the FPS meter from the start (for pads without R3) |
| `THUG_RENDER_SCALE=0.75` | Render fewer lines: faster on GPU-bound devices, softer picture |
| `THUG_RENDER_THREAD=0` | Single-threaded rendering, if graphics break |

The full list, including the diagnostic switches, is in
[`portmaster/README.md`](portmaster/README.md).

## Files on the device

- `ports/thug/log.txt`: the log of the last run. Attach it to bug reports.
- `ports/thug/patchlog.txt`: the log of the first-launch extraction.
- `ports/thug/save/`: your saves.

## Good to know

- **Videos** play from `data/movies`. Delete that folder to save space; the
  videos are then skipped.
- **No online or LAN play.** The game's online service shut down years ago.
- The picture fills any screen shape without stretching. The HUD and menus
  stay 4:3 in the middle.

## Building from source

See [`portmaster/README.md`](portmaster/README.md): an ARM hard-float cross
compiler on Debian 10 (glibc 2.28), SDL 2, a pinned and patched gl4es, and a
minimal FFmpeg build for the Bink videos. `portmaster/package.py` stages the
release folder.

### Repository layout

| Path | Contents |
|---|---|
| `Code/` | The engine. Platform backends live in `Code/*/Vita/`: written for the Vita port, they run on Linux here through a small vitaGL/libc shim |
| `portmaster/` | The Linux platform layer (SDL 2, input, threads, presenter), CMake build, launcher, installer and packaging |
| `vita/engine.cmake` | Which engine sources are built |
| `vita/qb/`, `vita/gen/` | The port's options script and its compiled form |
| `vita/tools/` | Data tools: QB script assembler, scene and texture readers, HD pack builder |

## How it was made

Like the Vita port, this one was built with agentic AI (Anthropic's Claude),
with me testing each build on the devices, reading the logs and comparing
against the Xbox version.

## Credits

- Neversoft and Activision made the game. This project isn't affiliated with
  or endorsed by them, and all trademarks belong to their owners.
- [kisak-thug](https://github.com/SwagSoftware/kisak-thug), the PC project this
  port builds on.
- [gl4es](https://github.com/ptitSeb/gl4es) by ptitSeb.
- [FFmpeg](https://ffmpeg.org) (LGPL 2.1+) for the Bink video decoder.
- [SDL](https://www.libsdl.org) and the [PortMaster](https://portmaster.games)
  team.
- [Anthropic](https://www.anthropic.com), whose Claude models did most of the
  porting work.

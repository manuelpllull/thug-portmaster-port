## Notes

Thanks to Neversoft and Activision for the game, the [kisak-thug](https://github.com/SwagSoftware/kisak-thug) project this port builds on, ptitSeb for [gl4es](https://github.com/ptitSeb/gl4es) and [FFmpeg](https://ffmpeg.org) for the Bink video decoder.

You need the **USA Xbox** version. Copy your own disc image (`.iso`) into `ports/thug/gamedata/`. The first launch extracts it there (about 15 minutes, needs python3 and 3.5 GB free); you can delete the `.iso` afterwards.

Without python3, extract it on a computer with [extract-xiso](https://github.com/XboxDev/extract-xiso) (`extract-xiso -x "Tony Hawk's Underground (USA).iso"`) and copy what it produces into `ports/thug/gamedata/`, so that `ports/thug/gamedata/data/pre/qb.prx` exists. The `data/movies` folder can be left out to save space; the videos are then skipped.

Saves go to `ports/thug/save/`. The first level load after installing takes longer while shaders are compiled.

If graphics break, create `ports/thug/thug.cfg` with the line `THUG_RENDER_THREAD=0`.

## Controls

| Button | Action |
|--|--|
| Left stick / D-pad | Skate, menus |
| B | Ollie, accept |
| Y | Flip trick |
| A | Grab trick, back |
| X | Grind, lip trick |
| L1 / R1 | Spin left / right |
| L2 / R2 | Nollie / switch stance, reverts |
| Select | Get off the board |
| Start | Pause |
| R3 + Select | FPS meter |
| Select + Start | Quit |

Face buttons go by their labels. To move an action, add a line to `ports/thug/thug.cfg`, for example `THUG_BTN_OLLIE=a` and `THUG_BTN_GRAB=b` to swap those two; the actions are `THUG_BTN_OLLIE`, `THUG_BTN_GRAB`, `THUG_BTN_FLIP` and `THUG_BTN_GRIND`, each `a`, `b`, `x` or `y`.

No R3? `THUG_FPS_METER=1` in `thug.cfg` shows the FPS meter from the start.

## Compile

Source and build instructions: https://github.com/manuelpllull/thug-portmaster-port (see `portmaster/README.md`).

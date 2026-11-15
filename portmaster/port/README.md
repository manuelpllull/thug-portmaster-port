## Notes

Thanks to Neversoft and Activision for the game, the [kisak-thug](https://github.com/SwagSoftware/kisak-thug) project this port builds on, ptitSeb for [gl4es](https://github.com/ptitSeb/gl4es) and [FFmpeg](https://ffmpeg.org) for the Bink video decoder.

You need the **USA Xbox** version. Extract your own disc image with [extract-xiso](https://github.com/XboxDev/extract-xiso) (`extract-xiso -x "Tony Hawk's Underground (USA).iso"`) and copy what it produces into `ports/thug/gamedata/`, so that `ports/thug/gamedata/data/pre/qb.prx` exists. The `data/movies` folder can be left out to save space; the videos are then skipped.

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

## Compile

Source and build instructions: https://github.com/manuelpllull/thug-portmaster-port (see `portmaster/README.md`).

# Tony Hawk's Underground on PortMaster

32-bit ARM (armhf) build for PortMaster handhelds, made and tested on the R36S (RK3326, 640x480, 1 GB).
The picture fills any screen shape without stretching: wider screens show more to the sides, taller ones
more above and below; the HUD and menus stay 4:3 in the middle, with menu backgrounds mirrored outwards.
The player-facing README, `port.json`, `gameinfo.xml` and screenshot are in `port/`; `THUG.sh` is the launcher.

## Installing

1. Install the release zip (`thug.zip`) through PortMaster's `autoinstall` folder, or extract it into `ports/`.
2. Extract your **USA Xbox** disc image with `extract-xiso -x "Tony Hawk's Underground (USA).iso"` and copy
   what it produces into `ports/thug/gamedata/`, so that `ports/thug/gamedata/data/pre/qb.prx` exists.
3. Start **THUG** from the Ports menu. The log is `ports/thug/log.txt`; saves are in `ports/thug/save/`.

## Switches

Optional `ports/thug/thug.cfg`, one `VAR=value` per line, read by the launcher:

| Variable | Default | Effect |
|--|--|--|
| `THUG_RENDER_THREAD` | 1 | GL calls replayed on a render thread. 0 = single thread |
| `THUG_PRESENTER` | 1 | presenter thread (needed for the FPS meter, R3 + Select) |
| `THUG_PACING_MODE` | vsync | `vsync`, `grid`, `locked`, `legacy`, `steady` (opt-in, not device-tested: every frame is shown for the same number of refreshes, so the frame rate is flat instead of hopping; see below) |
| `THUG_STEADY_VBLANKS` | 2 (3 above 100 Hz) | `steady`: refreshes per frame (60 Hz: 30 FPS, 77 Hz: 38.5 FPS) |
| `THUG_STEADY_ADAPT` | 1 | `steady`: when 10% of frames miss their slot the hold grows by one refresh (up to +2) and is retried shorter later; 0 = fixed |
| `THUG_PSA` | 1 | gl4es shader cache in `ports/thug/` |
| `THUG_RENDER_SCALE` | 1 | fraction of the render height to draw, 0.25–1: `0.75` on the RG34XX renders 360 lines instead of 480 (fewer GPU pixels, softer picture); applies after `THUG_RENDER_HEIGHT` |
| `THUG_RENDER_HEIGHT` | 480 | most lines the game renders; the presenter scales to the screen (raise on fast devices; **lower, e.g. 360, on a GPU-bound one** such as the RG34XX at 720x480: fewer pixels, softer picture, never a default) |
| `THUG_WINDOW_SIZE` | screen | `WxH` window size, for desktop tests |
| `THUG_FPS_METER` | 0 | FPS meter shown at start |
| `THUG_PERF` | 0 | frame timing lines in the log |
| `THUG_PROFILE` | 0 | sampling profiler, written at exit |
| `THUG_VERBOSE` | 0 | engine tracing |
| `THUG_CLEAN_QUIT` | 1 | on quit, end the render and presenter threads first and skip the GPU driver's teardown (it crashed on Knulli); 0 = plain `exit()` |
| `THUG_GL_LIB_SEARCH` | 1 | when `libEGL.so(.1)` does not load (Knulli: the bare names are the 64-bit driver), give SDL the 32-bit EGL/GLES by path from `/usr/lib32` or `arm-linux-gnueabihf`; 0 = off |

## Building

The Linux build uses CMake and an ARM hard-float compiler. It fetches the pinned GL4ES source at configure
time and patches it (`cmake/gl4es-userprog-state.patch`). SDL 2 headers and the cross-built SDL library
default to `/opt/r36s`; SDL itself is not packaged. Use a Debian 10 toolchain for a glibc 2.28 baseline.

For the movies, unpack [FFmpeg 7.1.1](https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz) and build its Bink
support. The packaged FFmpeg libraries are shared and licensed under LGPL 2.1 or later.

```sh
sh portmaster/build-ffmpeg-bink.sh /path/to/ffmpeg-7.1.1 build-ffmpeg build-armhf/ffmpeg

cmake -S portmaster -B build-armhf -G Ninja \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc \
  -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++ \
  -DTHUG_FFMPEG_BINK_DIR=build-armhf/ffmpeg
cmake --build build-armhf
arm-linux-gnueabihf-strip -o build-armhf/thug.stripped.armhf build-armhf/thug.armhf
python3 portmaster/package.py \
  --binary build-armhf/thug.stripped.armhf \
  --gl4es-license build-armhf/_deps/gl4es-src/LICENSE \
  --ffmpeg-dir build-armhf/ffmpeg \
  --out build-armhf/release
```

`build-armhf/release/thug/` is the PortMaster-New `ports/thug/` folder.

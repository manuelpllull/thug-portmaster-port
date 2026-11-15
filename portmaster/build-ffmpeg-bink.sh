#!/bin/sh
# Build the FFmpeg 7.1 Bink demuxer and decoders as ARM hard-float shared libraries.
set -eu

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 /path/to/ffmpeg-7.1.1 /path/to/build /path/to/prefix" >&2
    exit 2
fi

source_dir=$(cd "$1" && pwd)
mkdir -p "$2" "$3"
prefix=$(cd "$3" && pwd)
cd "$2"

"$source_dir/configure" \
    --prefix="$prefix" \
    --enable-cross-compile \
    --cross-prefix=arm-linux-gnueabihf- \
    --host-cc="${HOST_CC:-cc}" \
    --target-os=linux \
    --arch=arm \
    --cpu=cortex-a35 \
    --extra-cflags="-mfpu=neon-vfpv4 -mfloat-abi=hard" \
    --disable-everything \
    --enable-demuxer=bink \
    --enable-decoder=bink,binkaudio_dct,binkaudio_rdft \
    --enable-shared \
    --disable-static \
    --disable-programs \
    --disable-doc \
    --disable-network \
    --disable-autodetect \
    --disable-avdevice \
    --disable-swscale \
    --disable-swresample \
    --disable-avfilter \
    --disable-debug \
    --enable-pthreads

make -j"${JOBS:-4}"
make install
cp "$source_dir/COPYING.LGPLv2.1" "$prefix/"


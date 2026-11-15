#!/usr/bin/env python3
"""Stage the PortMaster submission folder <out>/thug/ (PortMaster-New ports/thug/ layout).

Game files are supplied by the player. SDL2 is not bundled: every firmware ships its own.
Zip it with PortMaster's tools/build_release.py, or any tool that puts THUG.sh at the zip
root and the port files in thug/.
"""

import argparse
import shutil
from pathlib import Path

FFMPEG_LIBS = ("libavcodec.so.61", "libavformat.so.61", "libavutil.so.59")


def check_armhf(parser, path, data):
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01" or data[18:20] != b"\x28\x00":
        parser.error(f"{path} must be a 32-bit little-endian ARM ELF")
    if not int.from_bytes(data[36:40], "little") & 0x400:
        parser.error(f"{path} must use the ARM hard-float ABI")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="stripped thug.armhf")
    parser.add_argument("--gl4es-license", type=Path, required=True)
    parser.add_argument("--ffmpeg-dir", type=Path, required=True, help="FFmpeg install prefix (Bink build)")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    top = args.out / "thug"
    if top.exists():
        shutil.rmtree(top)
    inner = top / "thug"
    (inner / "libs.armhf").mkdir(parents=True)
    (inner / "licenses").mkdir()

    for name in ("port.json", "README.md", "gameinfo.xml", "screenshot.png"):
        shutil.copyfile(here / "port" / name, top / name)
    (top / "THUG.sh").write_bytes((here / "THUG.sh").read_bytes().replace(b"\r\n", b"\n"))

    binary = args.binary.read_bytes()
    check_armhf(parser, args.binary, binary)
    (inner / "thug.armhf").write_bytes(binary)

    libdir = args.ffmpeg_dir / "lib"
    for soname in FFMPEG_LIBS:
        versioned = sorted(libdir.glob(f"{soname}.*"))
        if len(versioned) != 1:
            parser.error(f"expected one versioned library for {soname} in {libdir}")
        data = versioned[0].read_bytes()
        check_armhf(parser, versioned[0], data)
        (inner / "libs.armhf" / soname).write_bytes(data)

    shutil.copyfile(args.gl4es_license, inner / "licenses" / "LICENSE.gl4es.txt")
    shutil.copyfile(args.ffmpeg_dir / "COPYING.LGPLv2.1", inner / "licenses" / "LICENSE.FFmpeg.txt")
    print(top)


if __name__ == "__main__":
    main()

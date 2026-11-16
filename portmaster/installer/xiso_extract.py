#!/usr/bin/env python3
"""Extract an Xbox disc image (XDVDFS) into a folder, like extract-xiso -x.

Takes redump images (the game partition sits after the video partition) and
extract-xiso or trimmed images (the game partition starts at 0). Files go to
<out>/.xiso-tmp first and are moved into <out> only when every file is out,
so an interrupted run never leaves a half-installed game behind.

    xiso_extract.py IMAGE OUT
"""

import os
import shutil
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Where the game partition starts: extract-xiso/trimmed, XGD1 (redump), XGD2, XGD3.
PARTITIONS = (0, 0x18300000, 0x0FD90000, 0x02080000)
DIRECTORY = 0x10
CHUNK = 1 << 20


def fail(msg):
    print(msg, file=sys.stderr)
    sys.exit(1)


def find_partition(f):
    for base in PARTITIONS:
        f.seek(base + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            return base
    return None


def read_table(f, base, sector, size):
    if size == 0:
        return b""
    f.seek(base + sector * SECTOR)
    return f.read(size)


def entries(table):
    """Walk one directory's binary tree; yields (name, sector, size, attributes)."""
    if len(table) < 14 or table[:2] == b"\xff\xff":
        return
    todo, seen = [0], set()
    while todo:
        off = todo.pop()
        if off in seen or off + 14 > len(table):
            continue
        seen.add(off)
        left, right, sector, size, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
        if left == 0xFFFF:
            continue
        name = table[off + 14:off + 14 + nlen].decode("latin-1")
        if not name or name in (".", "..") or "/" in name or "\\" in name or "\0" in name:
            fail(f"Bad file name in the disc image: {name!r}")
        yield name, sector, size, attr
        if left:
            todo.append(left * 4)
        if right:
            todo.append(right * 4)


def list_files(f, base, root_sector, root_size):
    """Every file as (relative path, sector, size), and every directory."""
    files, dirs = [], []
    todo = [("", root_sector, root_size)]
    while todo:
        path, sector, size = todo.pop()
        for name, s, n, attr in entries(read_table(f, base, sector, size)):
            rel = os.path.join(path, name) if path else name
            if attr & DIRECTORY:
                dirs.append(rel)
                todo.append((rel, s, n))
            else:
                files.append((rel, s, n))
    return files, dirs


def main():
    if len(sys.argv) != 3:
        fail(__doc__.strip())
    image, out = sys.argv[1], sys.argv[2]
    tmp = os.path.join(out, ".xiso-tmp")

    with open(image, "rb") as f:
        base = find_partition(f)
        if base is None:
            fail(f"{os.path.basename(image)} is not an Xbox disc image.")
        f.seek(base + 32 * SECTOR + len(MAGIC))
        root_sector, root_size = struct.unpack("<II", f.read(8))
        files, dirs = list_files(f, base, root_sector, root_size)
        total = sum(n for _, _, n in files)
        if not files:
            fail("The disc image has no files.")

        os.makedirs(out, exist_ok=True)
        shutil.rmtree(tmp, ignore_errors=True)
        free = shutil.disk_usage(out).free
        if free < total + (64 << 20):
            fail(f"Not enough free space: {total / 1e9:.1f} GB needed, {free / 1e9:.1f} GB free.")

        print(f"{len(files)} files, {total / 1e9:.2f} GB", flush=True)
        os.makedirs(tmp)
        for d in dirs:
            os.makedirs(os.path.join(tmp, d), exist_ok=True)

        done, shown = 0, -1
        for rel, sector, size in files:
            f.seek(base + sector * SECTOR)
            with open(os.path.join(tmp, rel), "wb") as o:
                left = size
                while left:
                    data = f.read(min(CHUNK, left))
                    if not data:
                        fail(f"The disc image ends early (in {rel}).")
                    o.write(data)
                    left -= len(data)
            done += size
            pct = done * 100 // total
            if pct // 5 != shown:
                shown = pct // 5
                print(f"Extracting: {pct}% ({done / 1e9:.2f} of {total / 1e9:.2f} GB)", flush=True)

    for name in os.listdir(tmp):
        dst = os.path.join(out, name)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        elif os.path.exists(dst):
            os.remove(dst)
        os.replace(os.path.join(tmp, name), dst)
    os.rmdir(tmp)
    print("Done.", flush=True)


if __name__ == "__main__":
    main()

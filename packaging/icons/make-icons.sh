#!/bin/sh
# Regenerates the icon files from cutenes.svg (run on macOS, with
# rsvg-convert from librsvg). The outputs are committed, so a build never
# needs these tools:
#   cutenes.png   512x512, the Linux icon (AppImage, .desktop) and the
#                 window icon
#   cutenes.icns  the macOS bundle icon
#   cutenes.ico   the Windows executable icon (PNG-compressed entries)
set -eu
cd "$(dirname "$0")"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

rsvg-convert -w 512 -h 512 cutenes.svg -o cutenes.png

set_dir="$work/cutenes.iconset"
mkdir "$set_dir"
for size in 16 32 128 256 512; do
    rsvg-convert -w "$size" -h "$size" cutenes.svg -o "$set_dir/icon_${size}x${size}.png"
    double=$((size * 2))
    rsvg-convert -w "$double" -h "$double" cutenes.svg -o "$set_dir/icon_${size}x${size}@2x.png"
done
iconutil -c icns "$set_dir" -o cutenes.icns

for size in 16 24 32 48 64 128 256; do
    rsvg-convert -w "$size" -h "$size" cutenes.svg -o "$work/ico_$size.png"
done
python3 - "$work" <<'PY'
# An .ico is a directory of images; each entry here holds a PNG, which
# Windows Vista and later read directly.
import struct, sys, os
work = sys.argv[1]
sizes = [16, 24, 32, 48, 64, 128, 256]
images = [open(os.path.join(work, f"ico_{s}.png"), "rb").read() for s in sizes]
out = struct.pack("<HHH", 0, 1, len(sizes))
offset = 6 + 16 * len(sizes)
for s, data in zip(sizes, images):
    dim = 0 if s == 256 else s  # 0 means 256
    out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
with open("cutenes.ico", "wb") as f:
    f.write(out + b"".join(images))
PY
echo "wrote cutenes.png cutenes.icns cutenes.ico"

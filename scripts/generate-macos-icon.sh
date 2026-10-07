#!/usr/bin/env bash
set -euo pipefail
source_png="$1"
output="$2"
iconset="${output%.icns}.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
 /usr/bin/sips -z "$size" "$size" "$source_png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
 twice=$((size * 2))
 /usr/bin/sips -z "$twice" "$twice" "$source_png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
/usr/bin/iconutil -c icns "$iconset" -o "$output"

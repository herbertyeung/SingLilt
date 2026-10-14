#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
app="build/bin/Release/SingLilt.app"
./scripts/deploy-macos.sh "$app"
version=$(/usr/libexec/PlistBuddy -c 'Print CFBundleShortVersionString' "$app/Contents/Info.plist")
arch=$(/usr/bin/lipo -archs "$app/Contents/MacOS/SingLilt")
if [[ "$arch" != arm64 ]]; then echo 'This package currently supports a verified arm64 build only.' >&2; exit 1; fi
mkdir -p build/releases
archive="build/releases/SingLilt-$version-mac-arm64.zip"
/usr/bin/ditto -c -k --keepParent "$app" "$archive"
(cd build/releases && /usr/bin/shasum -a 256 "$(basename "$archive")" > "$(basename "$archive").sha256")
echo "$archive"

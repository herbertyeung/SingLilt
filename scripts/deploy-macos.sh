#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
app="${1:-build/bin/Release/SingLilt.app}"
qt_bin="${QT_BIN_DIR:-$(brew --prefix qtbase)/bin}"
"$qt_bin/macdeployqt" "$app" -always-overwrite -verbose=1
# Qt's deployment tool recursively closes non-system dylib dependencies too.
python3 scripts/fix-macos-loader-paths.py "$app"
python3 scripts/collect-macos-notices.py "$app"
# Sign nested Mach-O code after all loader-path modifications.
while IFS= read -r -d '' file; do
 if /usr/bin/file -b "$file" | /usr/bin/grep -q 'Mach-O'; then
  /usr/bin/codesign --force --sign - "$file"
 fi
done < <(find "$app/Contents" -type f -print0)
while IFS= read -r framework; do
 /usr/bin/codesign --force --sign - "$framework"
done < <(find "$app/Contents/Frameworks" -type d -name '*.framework' | sort -r)
/usr/bin/codesign --force --sign - "$app"
/usr/bin/codesign --verify --deep --strict "$app"

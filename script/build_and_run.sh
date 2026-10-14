#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode="${1:-run}"
./scripts/build-macos.sh
app="$(pwd)/build/bin/Release/SingLilt.app"
# Relinking restores development library paths and invalidates the bundle seal.
if ! /usr/bin/codesign --verify --deep --strict "$app" >/dev/null 2>&1; then
 ./scripts/deploy-macos.sh "$app"
fi
case "$mode" in
 run) /usr/bin/open -n "$app" ;;
 --debug) lldb -- "$app/Contents/MacOS/SingLilt" ;;
 --logs|--telemetry) /usr/bin/open -n "$app"; /usr/bin/log stream --info --predicate 'process == "SingLilt"' ;;
 --verify) "$app/Contents/MacOS/SingLilt" & launch_pid=$!; sleep 1; kill -0 "$launch_pid" ;;
 *) echo "Usage: $0 [run|--debug|--logs|--telemetry|--verify]" >&2;exit 2 ;;
esac

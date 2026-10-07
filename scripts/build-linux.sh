#!/usr/bin/env bash
# Configure, build and test Linux in the single build directory.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
configuration=${1:-Release}
mode=${2:-desktop}
case "$configuration" in Debug|Release) ;; *) printf 'Expected Debug or Release\n' >&2; exit 2 ;; esac
case "$mode" in desktop|core) ;; *) printf 'Expected desktop or core\n' >&2; exit 2 ;; esac
if [[ -f build/CMakeCache.txt ]] && ! grep -q '^CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config$' build/CMakeCache.txt; then
    printf 'build/ uses another generator. Use a separate Linux checkout, not the Windows build tree.\n' >&2
    exit 2
fi
if [[ -x build/tools/linux-env/bin/cmake ]]; then
    export PATH="$root/build/tools/linux-env/bin:$PATH"
fi
preset=linux
[[ "$mode" == core ]] && preset=linux-core
cmake --preset "$preset"
cmake --build --preset "$preset-${configuration,,}" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
QT_QPA_PLATFORM=offscreen ctest --preset "$preset-${configuration,,}"

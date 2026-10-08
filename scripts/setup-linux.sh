#!/usr/bin/env bash
# Download the same pinned Qt release as Windows without changing system Python.
set -euo pipefail
if [[ $(uname -s) != Linux || $(uname -m) != x86_64 ]]; then
    printf 'This dependency setup targets Linux x86-64. Supply your own Qt for another architecture.\n' >&2
    exit 2
fi
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
python3 -m venv build/tools/linux-env
build/tools/linux-env/bin/python -m pip install --disable-pip-version-check 'aqtinstall==3.3.0' 'cmake>=3.30,<4'
qt_root=build/_deps/Qt/6.8.3/gcc_64
if [[ ! -f "$qt_root/lib/cmake/Qt6Multimedia/Qt6MultimediaConfig.cmake" ||
      ! -f "$qt_root/lib/libicui18n.so.73" || ! -f "$qt_root/lib/libQt6Quick.so.6" ]]; then
    build/tools/linux-env/bin/python -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
        -O build/_deps/Qt --archives icu qtbase qtdeclarative qtmultimedia -m qtmultimedia
fi
"$qt_root/libexec/rcc" --version
plugin_dependencies=$(ldd "$qt_root/plugins/multimedia/libffmpegmediaplugin.so")
if grep -q 'not found' <<< "$plugin_dependencies"; then
    printf '%s\n' "$plugin_dependencies" >&2
    exit 1
fi
printf 'READY: Qt 6.8.3 and CMake in build/; run scripts/build-linux.sh\n'

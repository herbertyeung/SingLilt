#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset macos "$@"
cmake --build --preset macos-release --parallel 6
ctest --preset macos-release --output-on-failure -L 'core|native'

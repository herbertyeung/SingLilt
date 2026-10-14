#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Only required Qt modules; do not install every optional Qt product.
HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1 brew install cmake ninja pkgconf qtbase qtmultimedia qttools fluidsynth python@3.12
"$(brew --prefix python@3.12)/bin/python3.12" scripts/setup-macos-soundfonts.py
"$(brew --prefix python@3.12)/bin/python3.12" scripts/setup-macos-omr.py

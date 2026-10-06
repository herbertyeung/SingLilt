# Project-local desktop dependencies and optional audio-analysis setup.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param([switch]$WithAnalysis, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Invoke-Native.ps1"
$python = (Get-Command python -ErrorAction Stop).Source
$qt = Join-Path $root 'build/_deps/Qt/6.8.3/msvc2022_64'
New-Item -ItemType Directory -Force "$root/build/tools", "$root/build/_deps" | Out-Null
if (-not (Test-Path "$qt/bin/qmake.exe")) {
    Invoke-NativeCommand $python @('-m', 'pip', 'install', '--disable-pip-version-check', '--target',
        "$root/build/tools/aqt", 'aqtinstall==3.3.0')
    $previousPythonPath = $env:PYTHONPATH
    try {
        $env:PYTHONPATH = "$root/build/tools/aqt"
        Invoke-NativeCommand $python @('-m', 'aqt', 'install-qt', 'windows', 'desktop', '6.8.3',
            'win64_msvc2022_64', '-O', "$root/build/_deps/Qt", '--archives', 'qtbase') "$root/build"
    } finally {
        $env:PYTHONPATH = $previousPythonPath
    }
}
& "$PSScriptRoot/setup-soundfonts.ps1"
& "$PSScriptRoot/setup-gm-soundfont.ps1"
& "$PSScriptRoot/setup-native-staff-omr.ps1"
if ($WithAnalysis) {
    & "$PSScriptRoot/setup-audio.ps1"
    & "$PSScriptRoot/setup-separation.ps1"
}
if (-not $SkipBuild) {
    & "$PSScriptRoot/build.ps1" -Configuration Release
}

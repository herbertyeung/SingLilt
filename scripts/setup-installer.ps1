# Pinned project-local Inno Setup compiler installation.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Invoke-Native.ps1"
$tools = Join-Path $root 'build/tools/InnoSetup'
$compiler = Join-Path $tools 'ISCC.exe'
if (Test-Path -LiteralPath $compiler) {
    Write-Output "INSTALLER_COMPILER=$compiler"
    return
}
$download = Join-Path $root 'build/tools/innosetup-6.7.3.exe'
New-Item -ItemType Directory -Force (Split-Path -Parent $download) | Out-Null
if (-not (Test-Path -LiteralPath $download)) {
    Invoke-WebRequest -Uri 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $download
}
$expectedHash = '9c73c3bae7ed48d44112a0f48e66742c00090bdb5bef71d9d3c056c66e97b732'
if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Inno Setup download hash mismatch.'
}
Invoke-NativeCommand $download @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/PORTABLE=1', "/DIR=$tools")
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Inno Setup compiler was not installed.' }
Write-Output "INSTALLER_COMPILER=$compiler"

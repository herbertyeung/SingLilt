# Configure, build, deploy, and test the selected CMake preset.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$CoreOnly,
    [switch]$CiTests
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Invoke-Native.ps1"
$cmake = (Get-Command cmake -ErrorAction Stop).Source
$configurePreset = if ($CoreOnly) { 'core' } else { 'vs2026' }
$buildPreset = if ($CoreOnly) { "core-$($Configuration.ToLowerInvariant())" } else { $Configuration.ToLowerInvariant() }
$configureArguments = @('--preset', $configurePreset)
if (-not $CoreOnly) {
    $audioOutput = if ($CiTests) { 'OFF' } else { 'ON' }
    $configureArguments += "-DSINGLILT_TEST_AUDIO_OUTPUT=$audioOutput"
}
Invoke-NativeCommand $cmake $configureArguments
Invoke-NativeCommand $cmake @('--build', '--preset', $buildPreset, '--parallel', '6')
if ($CoreOnly) {
    Invoke-NativeCommand (Get-Command ctest -ErrorAction Stop).Source @('--test-dir', "$root/build", '-C', $Configuration, '--output-on-failure')
    Write-Output "READY: $root/build/$Configuration/SingLiltCoreTests.exe"
    return
}
$testArguments = @('--test-dir', "$root/build", '-C', $Configuration, '--output-on-failure')
if ($CiTests) { $testArguments += @('-L', 'ci') }
Invoke-NativeCommand (Get-Command ctest -ErrorAction Stop).Source $testArguments
$solution = if (Test-Path "$root/build/SingLilt.slnx") { 'SingLilt.slnx' } else { 'SingLilt.sln' }
$filter = @{ solution = @{ path = $solution; projects = @('SingLilt.vcxproj', 'SingLiltCoreTests.vcxproj', 'SingLiltLanguageTests.vcxproj', 'SingLiltMigrationTests.vcxproj', 'SingLiltAudioDecoderTests.vcxproj') } }
$filter | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$root/build/SingLilt.slnf" -Encoding utf8
Write-Output "READY: $root/build/bin/$Configuration/SingLilt.exe"

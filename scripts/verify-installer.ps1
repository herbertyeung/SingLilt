# Isolated install, reinstall, and uninstall verification.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [Parameter(Mandatory)][string]$Installer,
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Invoke-Native.ps1"
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'build/installer-tests' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$install = Join-Path $output ('installed-' + [guid]::NewGuid().ToString('N'))
$registryKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{A3C44ECB-9947-4F6E-9414-53054F73B742}_is1'
if (Test-Path -LiteralPath $registryKey) { throw 'An installed SingLilt must be removed before this isolated installer test.' }
$sidecar = Get-Content -LiteralPath "$installerPath.sha256" -Raw
$expectedHash = ($sidecar.Trim() -split '\s+')[0]
if ((Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash -ne $expectedHash) {
    throw 'Installer hash mismatch.'
}
$checks = [Collections.Generic.List[string]]::new()
$group = 'SingLilt-InstallerTest-' + [guid]::NewGuid().ToString('N')
$arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$install", "/GROUP=$group")
$uninstaller = Join-Path $install 'unins000.exe'
try {
    Invoke-NativeCommand $installerPath $arguments
    $manifest = Get-Content -LiteralPath (Join-Path $install 'package-manifest.json') -Raw | ConvertFrom-Json
    foreach ($file in $manifest.files) {
        if ((Get-FileHash -LiteralPath (Join-Path $install $file.path) -Algorithm SHA256).Hash -ne $file.sha256) {
            throw "Installed file differs from the payload: $($file.path)"
        }
    }
    $checks.Add('installed payload hashes match')
    $version = Invoke-NativeCommand (Join-Path $install 'SingLilt.exe') @('--version', '--language', 'en_US')
    if ($version -ne "SingLilt $($manifest.version)") { throw 'Installed executable did not start correctly.' }
    $checks.Add('installed executable version matches')
    $shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) "$group/SingLilt.lnk"
    if (-not (Test-Path -LiteralPath $shortcut)) { throw 'Start menu shortcut was not created.' }
    $checks.Add('Start menu shortcut exists')
    $sentinel = Join-Path $install 'user-project.jpp'
    [IO.File]::WriteAllText($sentinel, 'installer preservation test')
    $sentinelHash = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
    Invoke-NativeCommand $installerPath $arguments
    if ((Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -ne $sentinelHash) {
        throw 'Reinstallation changed the user project.'
    }
    $checks.Add('same-version reinstall preserves user project')
} finally {
    if (Test-Path -LiteralPath $uninstaller) {
        Invoke-NativeCommand $uninstaller @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-')
    }
}
if (Test-Path -LiteralPath (Join-Path $install 'SingLilt.exe')) { throw 'Uninstall left the application executable.' }
if ((Test-Path -LiteralPath $registryKey) -or (Test-Path -LiteralPath $shortcut)) {
    throw 'Uninstall left registration or Start menu entries.'
}
if ((Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash -ne $sentinelHash) {
    throw 'Uninstall removed or changed the user project.'
}
$checks.Add('uninstall removes app and preserves user project')
@{ passed = $true; installer = $installerPath; installDirectory = $install; checks = @($checks) } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
Write-Output "INSTALLER_TEST checks=$($checks.Count) status=PASS"

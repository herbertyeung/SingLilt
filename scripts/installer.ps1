# Validate a portable payload and compile its Windows installer.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [string]$OutputDirectory = '',
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/Invoke-Native.ps1"
$payload = (Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $payload 'package-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.schema -ne 1 -or $manifest.product -ne 'SingLilt' -or
    $manifest.version -notmatch '^\d+\.\d+\.\d+$' -or $manifest.edition -notin 'core', 'full' -or
    $manifest.executable -ne 'SingLilt.exe') {
    throw 'Unsupported package manifest.'
}
$names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($file in $manifest.files) {
    $path = [IO.Path]::GetFullPath((Join-Path $payload $file.path))
    if (-not $path.StartsWith($payload.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        -not $names.Add($path)) {
        throw "Invalid package entry: $($file.path)"
    }
    if ((Get-Item -LiteralPath $path).Length -ne $file.bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $file.sha256) {
        throw "Package entry changed: $($file.path)"
    }
}
$manifestPath = Join-Path $payload 'package-manifest.json'
$actualFiles = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Where-Object FullName -ne $manifestPath)
if ($actualFiles.Count -ne $names.Count) { throw 'Package contains unlisted files.' }
if (-not $Compiler) { $Compiler = Join-Path $root 'build/tools/InnoSetup/ISCC.exe' }
if (-not (Test-Path -LiteralPath $Compiler)) { throw 'Run scripts/setup-installer.ps1 first, or specify -Compiler.' }
if (-not $OutputDirectory) { $OutputDirectory = Split-Path -Parent $payload }
$output = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$name = "SingLilt-$($manifest.version)-win-x64-$($manifest.edition)-setup.exe"
$installer = Join-Path $output $name
if (Test-Path -LiteralPath $installer) { throw "Installer already exists: $installer" }
Invoke-NativeCommand $Compiler @("/DPayloadDir=$payload", "/DAppVersion=$($manifest.version)",
    "/DEdition=$($manifest.edition)", "/DOutputDir=$output", "$root/packaging/SingLilt.iss")
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name" | Set-Content -LiteralPath "$installer.sha256" -Encoding ascii
Write-Output "INSTALLER=$installer"

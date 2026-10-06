# Corresponding library-source archives and release hashes.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param([string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$version = [regex]::Match((Get-Content "$root/CMakeLists.txt" -Raw), 'project\(SingLilt VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'CMake version is missing.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'build/releases' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$cache = Join-Path $root 'build/_deps/SourceArchives'
New-Item -ItemType Directory -Force -Path $output, $cache | Out-Null
$archive = Join-Path $output "SingLilt-$version-third-party-sources.zip"
if (Test-Path -LiteralPath $archive) { throw "Source archive already exists: $archive" }
$sources = Get-Content -LiteralPath "$root/packaging/third-party-sources.json" -Raw | ConvertFrom-Json
foreach ($source in $sources) {
    $path = Join-Path $cache $source.name
    if (-not (Test-Path -LiteralPath $path)) {
        Invoke-WebRequest -Uri $source.url -OutFile "$path.part"
        if ((Get-FileHash -LiteralPath "$path.part" -Algorithm SHA256).Hash -ne $source.sha256) {
            throw "Source download hash mismatch: $($source.name)"
        }
        Move-Item -LiteralPath "$path.part" -Destination $path
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $source.sha256) {
        throw "Cached source hash mismatch: $($source.name)"
    }
}
$staging = [IO.Path]::GetFullPath((Join-Path $output ('.sources-' + [guid]::NewGuid().ToString('N'))))
if (-not $staging.StartsWith($output.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Source staging path must remain inside the output directory.'
}
New-Item -ItemType Directory -Path $staging | Out-Null
try {
    foreach ($source in $sources) { Copy-Item -LiteralPath (Join-Path $cache $source.name) -Destination $staging }
    Copy-Item -LiteralPath "$root/packaging/third-party-sources.json" -Destination "$staging/sources.json"
    Copy-Item -LiteralPath "$root/licenses/LGPL-3.0-only.txt" -Destination $staging
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($staging, $archive, [IO.Compression.CompressionLevel]::Fastest, $false)
} finally {
    Remove-Item -LiteralPath $staging -Recurse -Force
}
"$((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($archive))" |
    Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
Write-Output "THIRD_PARTY_SOURCES=$archive"

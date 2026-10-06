# Portable payload assembly, file hashes, and ZIP output.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [string]$OutputDirectory = '',
    [string]$MsvcRuntimeDirectory = '',
    [string]$SourceExecutable = '',
    [string]$SampleProject = '',
    [switch]$IncludeAnalysis,
    [switch]$Zip
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $root "build/bin/$Configuration"
$executable = if ($SourceExecutable) { [IO.Path]::GetFullPath($SourceExecutable) } else { Join-Path $runtime 'SingLilt.exe' }
if (-not ([IO.Path]::GetFullPath($executable).StartsWith([IO.Path]::GetFullPath($runtime).TrimEnd('\') + '\',
                                                      [StringComparison]::OrdinalIgnoreCase))) {
    throw 'The package executable must belong to the selected Release runtime.'
}
if (-not (Test-Path -LiteralPath $executable)) { throw 'Build the application before packaging.' }
$version = (Get-Item -LiteralPath $executable).VersionInfo.ProductVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'The executable has no valid product version.' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'build/releases' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$edition = if ($IncludeAnalysis) { 'full' } else { 'core' }
$name = "SingLilt-$version-win-x64-$edition"
$package = Join-Path $output $name
if (Test-Path -LiteralPath $package) { throw "Package already exists: $package" }
if ($Zip -and (Test-Path -LiteralPath "$package.zip")) { throw "Archive already exists: $package.zip" }
New-Item -ItemType Directory -Path $output -Force | Out-Null
$staging = Join-Path $output ".$name-staging-$([guid]::NewGuid().ToString('N'))"
foreach ($target in $staging, $package) {
    if (-not ([IO.Path]::GetFullPath($target).StartsWith($output.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar,
                                                      [StringComparison]::OrdinalIgnoreCase))) {
        throw 'Package target must remain inside the selected output directory.'
    }
}
New-Item -ItemType Directory -Path $staging | Out-Null

function Copy-PackageFile([string]$Source, [string]$RelativePath) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Required package file is missing: $Source" }
    $target = Join-Path $staging $RelativePath
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $target
}
function Copy-PackageDirectory([string]$Source, [string]$RelativePath) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw "Required package folder is missing: $Source" }
    foreach ($file in Get-ChildItem -LiteralPath $Source -Recurse -File) {
        Copy-PackageFile $file.FullName (Join-Path $RelativePath $file.FullName.Substring($Source.Length + 1))
    }
}

Copy-PackageFile $executable 'SingLilt.exe'
foreach ($file in Get-ChildItem -LiteralPath $runtime -File -Filter '*.dll') {
    Copy-PackageFile $file.FullName $file.Name
}
if (-not $MsvcRuntimeDirectory) {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Specify the x64 MSVC Redist CRT directory with -MsvcRuntimeDirectory.' }
    $installation = (& $vswhere -latest -products '*' -property installationPath | Select-Object -First 1)
    if (-not $installation) { throw 'Visual Studio Redist files were not found.' }
    $crt = Get-ChildItem -Path "$installation/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory |
        Sort-Object @{ Expression = { [version]$_.Parent.Parent.Name }; Descending = $true } |
        Select-Object -First 1
    if (-not $crt) { throw 'Specify the x64 MSVC Redist CRT directory with -MsvcRuntimeDirectory.' }
    $MsvcRuntimeDirectory = $crt.FullName
}
$crtFiles = @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'msvcp140_atomic_wait.dll',
              'vcruntime140.dll', 'vcruntime140_1.dll')
foreach ($file in $crtFiles) { Copy-PackageFile (Join-Path $MsvcRuntimeDirectory $file) $file }
foreach ($folder in 'platforms', 'imageformats', 'styles', 'tls', 'networkinformation', 'generic') {
    $source = Join-Path $runtime $folder
    if (Test-Path -LiteralPath $source) { Copy-PackageDirectory $source $folder }
}
foreach ($required in (@('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
                        'libfluidsynth-3.dll', 'SDL3.dll', 'sndfile.dll', 'platforms/qwindows.dll') + $crtFiles)) {
    if (-not (Test-Path -LiteralPath (Join-Path $staging $required))) { throw "Runtime deployment is incomplete: $required" }
}
Copy-PackageDirectory (Join-Path $root 'assets') 'assets'
Copy-PackageDirectory (Join-Path $runtime 'language') 'language'
if ($SampleProject) {
    Copy-PackageFile $SampleProject 'assets/staff-samples/user-native.jpp'
    @'
@echo off
start "" "%~dp0SingLilt.exe" "%~dp0assets\staff-samples\user-native.jpp"
'@ | Set-Content -LiteralPath (Join-Path $staging 'Open-recognized-score.cmd') -Encoding ascii
}
Copy-PackageFile (Join-Path $runtime 'assets/soundfonts/Salamander.sf2') 'assets/soundfonts/Salamander.sf2'
$gm = Join-Path $runtime 'assets/soundfonts/GeneralUser-GS.sf2'
if (Test-Path -LiteralPath $gm) { Copy-PackageFile $gm 'assets/soundfonts/GeneralUser-GS.sf2' }
Copy-PackageDirectory (Join-Path $root 'licenses') 'licenses'
Copy-PackageDirectory (Join-Path $runtime 'tools/omr-native') 'tools/omr-native'
foreach ($file in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Copy-PackageFile (Join-Path $MsvcRuntimeDirectory $file) "tools/omr-native/$file"
}
foreach ($file in 'README.md', 'THIRD_PARTY_NOTICES.md', 'CHANGELOG.md', 'LICENSE') {
    Copy-PackageFile (Join-Path $root $file) $file
}
foreach ($file in 'QUICK_START.md', 'OPTIONS.md', 'PROJECT_FORMAT.md', 'COURSES.md', 'BRAND.md', 'STAFF_NOTATION.md', 'TROUBLESHOOTING.md', 'LANGUAGE_PACKS.md') {
    Copy-PackageFile (Join-Path $root "docs/$file") "docs/$file"
}
Copy-PackageFile (Join-Path $root 'resources/branding/singlilt.ico') 'SingLilt.ico'

if ($IncludeAnalysis) {
    foreach ($required in 'tools/whisper/whisper-cli.exe', 'models/ggml-base.bin',
                          'tools/separation/python/python.exe', 'tools/separation/separate_vocals.py',
                          'tools/separation/models/model.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $runtime $required))) { throw "Analysis deployment is incomplete: $required" }
    }
    foreach ($folder in 'tools/whisper', 'tools/separation') {
        Copy-PackageDirectory (Join-Path $runtime $folder) $folder
    }
    Copy-PackageDirectory (Join-Path $runtime 'models') 'models'
}

$files = @(Get-ChildItem -LiteralPath $staging -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        path = $_.FullName.Substring($staging.Length + 1).Replace('\', '/')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$totalBytes = ($files | ForEach-Object { [long]$_['bytes'] } | Measure-Object -Sum).Sum
$manifest = [ordered]@{
    schema = 1
    product = 'SingLilt'
    version = $version
    architecture = 'x64'
    edition = $edition
    executable = 'SingLilt.exe'
    analysisIncluded = [bool]$IncludeAnalysis
    totalBytes = $totalBytes
    files = $files
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $staging 'package-manifest.json') -Encoding utf8
foreach ($file in $files) {
    $actual = (Get-FileHash -LiteralPath (Join-Path $staging $file.path) -Algorithm SHA256).Hash
    if ($actual -ne $file.sha256) { throw "Package verification failed: $($file.path)" }
}
# Publish only after the staged file set and hashes have been verified.
Move-Item -LiteralPath $staging -Destination $package
if ($Zip) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($package, "$package.zip", [IO.Compression.CompressionLevel]::Fastest, $true)
    $archive = "$package.zip"
    "$((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())  $([IO.Path]::GetFileName($archive))" |
        Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
    Write-Output "ARCHIVE=$archive"
}
Write-Output "PACKAGE=$package"
Write-Output "PACKAGE_VERIFIED files=$($files.Count) bytes=$totalBytes version=$version edition=$edition"

# Pinned native OMR runtime, model, and attribution setup.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path -Parent $PSScriptRoot
$dependency = Join-Path $root 'build/_deps/CrispEmbed'
$download = Join-Path $dependency 'downloads'
$models = Join-Path $dependency 'models'
$licenses = Join-Path $dependency 'licenses'
$runtime = Join-Path $dependency 'runtime'
New-Item -ItemType Directory -Path $download, $models, $licenses, $runtime -Force | Out-Null

function Get-VerifiedFile([string]$Url, [string]$Path, [string]$Sha256) {
    if (-not (Test-Path -LiteralPath $Path)) {
        $partial = "$Path.part"
        Invoke-WebRequest -Uri $Url -OutFile $partial
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Sha256) {
            throw "Download hash mismatch: $Path"
        }
        Move-Item -LiteralPath $partial -Destination $Path
    }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Sha256) {
        throw "Cached hash mismatch: $Path"
    }
}

$version = 'v0.17.12'
$commit = '78493a31fc3043f3af4ab6fd2f197a32da6f07f0'
$archiveUrl = "https://github.com/CrispStrobe/CrispEmbed/releases/download/$version/crispembed-windows-x86_64.zip"
$archiveHash = '49A1E4A89BF6F7D2DF100794914F15AEB9A2B69513C3DBFB93364208994CC717'
$archive = Join-Path $download "crispembed-$version-windows-x86_64.zip"
Get-VerifiedFile $archiveUrl $archive $archiveHash
$extraction = Join-Path $dependency $version
Expand-Archive -LiteralPath $archive -DestinationPath $extraction -Force

$modelName = 'Transcoda-59M-Q8'
$modelRevision = 'c4ed06a104f30d19503aa2ef8f79a23152167faa'
$modelHash = 'A6A977676339A7ADD577AA95089C2D30FB912B185EF117B068FE826E91A8A283'
$modelUrl = "https://huggingface.co/cstr/transcoda-omr-GGUF/resolve/$modelRevision/transcoda-q8_0.gguf"
$model = Join-Path $models 'transcoda-q8_0.gguf'
Get-VerifiedFile $modelUrl $model $modelHash

$licenseSources = @(
    @{ name = 'GGML-LICENSE.txt';
       url = 'https://raw.githubusercontent.com/CrispStrobe/ggml/890278a8342c620197c90e702e1188bcab94f510/LICENSE';
       sha256 = '94F29BBED6A22C35B992C5C6EBF0E7C92F13B836B90F36F461C9CF2F0F1D010D' },
    @{ name = 'TRANSCODA-MODEL-CARD.md';
       url = "https://huggingface.co/cstr/transcoda-omr-GGUF/raw/$modelRevision/README.md";
       sha256 = '8C0FB26A862C6087E512BAB263828BFDB9C7C08DFF8C01427218EB561CFA0940' },
    @{ name = 'CC-BY-4.0.txt';
       url = 'https://creativecommons.org/licenses/by/4.0/legalcode.txt';
       sha256 = '9BA9550AD48438D0836DDAB3DA480B3B69FFA0AAC7B7878B5A0039E7AB429411' }
)
foreach ($source in $licenseSources) {
    Get-VerifiedFile $source.url (Join-Path $licenses $source.name) $source.sha256
}

foreach ($name in 'crispembed.exe', 'ggml.dll', 'ggml-base.dll', 'ggml-cpu.dll') {
    $source = Join-Path $extraction $name
    if (-not (Test-Path -LiteralPath $source)) { throw "Incomplete native OMR runtime: $name" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $runtime $name) -Force
}
$runtimeModels = Join-Path $runtime 'models'
$runtimeLicenses = Join-Path $runtime 'licenses'
New-Item -ItemType Directory -Path $runtimeModels, $runtimeLicenses -Force | Out-Null
Copy-Item -LiteralPath $model -Destination (Join-Path $runtimeModels 'staff.gguf') -Force
Copy-Item -LiteralPath (Join-Path $extraction 'LICENSE') `
    -Destination (Join-Path $runtimeLicenses 'CRISPEMBED-LICENSE.txt') -Force
foreach ($source in $licenseSources) {
    Copy-Item -LiteralPath (Join-Path $licenses $source.name) `
        -Destination (Join-Path $runtimeLicenses $source.name) -Force
}
@'
CrispEmbed v0.17.12 Windows x86_64 CPU CLI and ggml libraries are unmodified.
CrispEmbed and ggml code: MIT. See the included license texts.
Transcoda-59M-Q8 weights: CC-BY-4.0, converted by CrispStrobe/cstr.
Original model: btrkeks/transcoda-59M-zeroshot-v1 by btrkeks.
Paper: Transcoda, arXiv:2605.10835.
Encoder backbone: facebook/convnextv2-tiny-22k-224.
GGUF model card: https://huggingface.co/cstr/transcoda-omr-GGUF
CrispEmbed implements native inference without copying the AGPL reference code,
according to its model card. The reference project's code is not distributed here.
Only the selected Transcoda weights are deployed as models/staff.gguf.
SMT evaluation weights and the release ZIP remain outside this runtime directory.
The child process requires the Microsoft Visual C++ x64 runtime and Windows UCRT.
'@ | Set-Content -LiteralPath (Join-Path $runtimeLicenses 'SOURCE.txt') -Encoding utf8

$files = foreach ($file in Get-ChildItem -LiteralPath $runtime -Recurse -File) {
    if ($file.Name -eq 'runtime-manifest.json') { continue }
    @{ path = [IO.Path]::GetRelativePath($runtime, $file.FullName); bytes = $file.Length;
       sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash }
}
$manifest = @{
    schemaVersion = 1
    engineVersion = $version
    engineCommit = $commit
    researchCommit = '71401aa9810c443936929a6c49696b90e6e789bc'
    backend = 'cpu'
    notationFormat = 'kern'
    modelName = $modelName
    modelRevision = $modelRevision
    modelSha256 = $modelHash
    modelLicense = 'CC-BY-4.0'
    modelPath = 'models/staff.gguf'
    cliArguments = @('-m', 'models/staff.gguf', '-t', '4', '--offline', '--ocr', 'INPUT.png')
    hostRuntimeRequirements = @('Microsoft Visual C++ x64 runtime', 'Windows UCRT')
    sources = @(
        @{ name = 'CrispEmbed release'; url = $archiveUrl; sha256 = $archiveHash; license = 'MIT' },
        @{ name = $modelName; url = $modelUrl; sha256 = $modelHash; license = 'CC-BY-4.0' }
    ) + $licenseSources
    files = @($files)
}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runtime 'runtime-manifest.json') -Encoding utf8
Write-Output "LOCAL_NATIVE_STAFF_ENGINE_READY=$runtime"
Write-Output "LOCAL_NATIVE_STAFF_MODEL=$modelName SHA256=$modelHash"
Write-Output "LOCAL_NATIVE_STAFF_RUNTIME_FILES=$(@($files).Count) version=$version backend=cpu"

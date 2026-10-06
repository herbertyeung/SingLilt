# Classroom playback, recording, and assessment checks.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [switch]$MicrophoneCheck
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
ctest --test-dir build -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Existing regression suite failed: $LASTEXITCODE" }
$folder = Join-Path $root "build/singing-school/check-$Configuration-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $folder | Out-Null
$report = Join-Path $folder 'report.json'
$extra = if ($MicrophoneCheck) { '--microphone-check' } else { '' }
$executable = Join-Path $root "build/bin/$Configuration/SingLilt.exe"
$process = Start-Process -FilePath $executable -ArgumentList "--classroom-check $extra --language zh_CN --report `"$report`"" -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Classroom check failed: exit=$($process.ExitCode); report=$report" }
$summary = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
if (-not $summary.passed) { throw "Classroom checks failed: $report" }
Write-Output "CLASSROOM $Configuration $($summary.checks.Count)/$($summary.checks.Count) PASS exit=0"
Write-Output "REPORT=$report"

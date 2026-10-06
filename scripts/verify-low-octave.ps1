# Lower-octave recognition checks using private score fixtures.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('baseline', 'modified', 'rollback')]
    [string]$Label = 'modified',
    [string]$Executable = '',
    [string]$OutputDirectory = '',
    [string]$FixtureDirectory = ''
)

# Focused image-recognition regression; no new target or broad test suite.
# Expected cases are anchored to original-image digit centers and degree, not
# just recognition IDs. All generated evidence stays under the one build tree.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $root 'build/low-octave-fix'
} elseif ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $root $OutputDirectory
}
$out = [System.IO.Path]::GetFullPath($out)
$buildPrefix = [System.IO.Path]::GetFullPath((Join-Path $root 'build')) + [System.IO.Path]::DirectorySeparatorChar
if (-not $out.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be a subdirectory of the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $root "build/bin/$Configuration/SingLilt.exe"
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $FixtureDirectory) { $FixtureDirectory = Join-Path $root 'build/private-fixtures' }
$image = (Resolve-Path -LiteralPath (Join-Path $FixtureDirectory 'buxia.png')).Path
$manualPath = Join-Path $FixtureDirectory 'buxia-intro.jpp'
$report = Join-Path $out 'report.json'
$savedReport = Join-Path $out "$Label-report.json"
$stdout = Join-Path $out "$Label-recognize.stdout.txt"
$stderr = Join-Path $out "$Label-recognize.stderr.txt"
$summaryPath = Join-Path $out "$Label-summary.json"
$logPath = Join-Path $out "$Label-results.txt"
$checks = New-Object 'System.Collections.Generic.List[object]'
$lines = New-Object 'System.Collections.Generic.List[string]'

function Add-Check([string]$Name, [bool]$Passed, [string]$Detail) {
    $checks.Add([pscustomobject]@{ name = $Name; passed = $Passed; detail = $Detail })
    $state = if ($Passed) { 'PASS' } else { 'FAIL' }
    $line = "$state $Name : $Detail"
    $lines.Add($line)
    Write-Output $line
}

$command = '"' + $Executable + '" --recognize "' + $image + '" --report "' + $report + '"'
$exeHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
$processExit = $null
try {
    # A fresh hidden CLI process only; do not stop or interact with open windows.
    $arguments = @('--recognize', ('"' + $image + '"'), '--report', ('"' + $report + '"'))
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments `
        -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $processExit = $process.ExitCode
    Add-Check 'recognizer process' ($processExit -eq 0) "exit=$processExit"
    if ($processExit -ne 0) { throw 'Recognition process did not complete successfully.' }
    Copy-Item -LiteralPath $report -Destination $savedReport -Force
    $data = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Add-Check 'total note count' ($data.noteCount -eq 393 -and $data.notes.Count -eq 393) `
        "expected=393 actual=$($data.notes.Count)"
    Add-Check 'valid timeline' ([bool]$data.validTimeline) "valid=$($data.validTimeline)"
    Add-Check 'key, quarter tempo and meter' `
        ($data.tonic -eq 1 -and $data.bpm -eq 58 -and $data.beatsPerBar -eq 4 -and $data.beatUnit -eq 4) `
        "tonic=$($data.tonic) bpm=$($data.bpm) meter=$($data.beatsPerBar)/$($data.beatUnit)"

    # sourceId, degree, image center x/y, expected octave, ticks, MIDI pitch.
    # Positive cases cover the user-reported 5 and adjacent low-register notes.
    # Undotted 1 at source59 and lyric-bearing source48..54 are negative controls.
    $cases = @(
        @(55, 6, 347.0, 220.0, -1, 480, 58),
        @(56, 6, 380.0, 220.0, -1, 120, 58),
        @(57, 6, 402.5, 220.0, -1, 120, 58),
        @(58, 7, 425.0, 220.0, -1, 120, 60),
        @(59, 1, 446.5, 220.0,  0, 120, 61),
        @(60, 7, 470.0, 220.0, -1, 120, 60),
        @(61, 6, 492.0, 220.0, -1, 120, 58),
        @(62, 5, 514.0, 220.0, -1, 240, 56),
        @(63, 6, 536.5, 220.0, -1, 960, 58),
        @(48, 3, 190.5, 220.0,  0, 120, 65),
        @(49, 2, 213.0, 220.0,  0, 120, 63),
        @(50, 3, 235.0, 220.0,  0, 240, 65),
        @(51, 3, 257.5, 220.0,  0, 120, 65),
        @(52, 2, 280.0, 220.0,  0, 120, 63),
        @(53, 3, 302.0, 220.0,  0, 240, 65),
        @(54, 1, 324.0, 220.0,  0, 240, 61)
    )
    $majorScale = @(0, 2, 4, 5, 7, 9, 11)
    $effectiveTonics = @{}
    $tonic = [int]$data.tonic
    foreach ($note in $data.notes) {
        if ([int]$note.keyOverride -ge 0) { $tonic = [int]$note.keyOverride }
        $effectiveTonics[[int]$note.id] = $tonic
    }
    foreach ($case in $cases) {
        $found = @($data.notes | Where-Object {
            [int]$_.degree -eq $case[1] -and $_.bbox.Count -eq 4 -and
            [math]::Abs(([double]$_.bbox[0] + [double]$_.bbox[2] / 2) - $case[2]) -le 3 -and
            [math]::Abs(([double]$_.bbox[1] + [double]$_.bbox[3] / 2) - $case[3]) -le 3
        })
        if ($found.Count -ne 1) {
            Add-Check "note $($case[0])" $false "degree=$($case[1]) center=($($case[2]),$($case[3])) matches=$($found.Count)"
            continue
        }
        $note = $found[0]
        $pitch = 60 + $effectiveTonics[[int]$note.id] + $majorScale[[int]$note.degree - 1] +
            12 * [int]$note.octave + [int]$note.accidental
        $ok = [int]$note.id -eq $case[0] -and [int]$note.octave -eq $case[4] -and
            [int]$note.durationTicks -eq $case[5] -and $pitch -eq $case[6]
        Add-Check "note $($case[0])" $ok `
            "locatedId=$($note.id) degree=$($note.degree) octave=$($note.octave)/$($case[4]) midi=$pitch/$($case[6]) ticks=$($note.durationTicks)/$($case[5])"
    }

    $manual = Get-Content -LiteralPath $manualPath -Raw | ConvertFrom-Json
    $introDifferences = New-Object 'System.Collections.Generic.List[string]'
    if ($manual.notes.Count -ne 47 -or $data.notes.Count -lt 47) {
        $introDifferences.Add('The 47-note intro is missing or incomplete.')
    } else {
        foreach ($index in 0..46) {
            foreach ($field in @('degree', 'octave', 'accidental', 'durationTicks', 'measure', 'line', 'keyOverride', 'tieToNext')) {
                if ($manual.notes[$index].$field -ne $data.notes[$index].$field) {
                    $introDifferences.Add("source$index/$field expected=$($manual.notes[$index].$field) actual=$($data.notes[$index].$field)")
                }
            }
        }
    }
    $introDetail = if ($introDifferences.Count -eq 0) {
        '47 notes retain degree/octave/accidental/duration/measure/line/key/tie fields from the explicit manual asset.'
    } else { $introDifferences -join '; ' }
    Add-Check 'manual intro remains unchanged' ($introDifferences.Count -eq 0) $introDetail

    $sourceTicks = ($data.notes | Measure-Object durationTicks -Sum).Sum
    Add-Check 'source durations unchanged' ($sourceTicks -eq 81600) "ticks=$sourceTicks expected=81600"
    $repeatOK = $data.repeats.Count -eq 1 -and $data.repeats[0].firstNote -eq 47 -and
        $data.repeats[0].endNote -eq 296 -and $data.repeats[0].count -eq 2 -and
        $data.repeats[0].firstEndingNote -eq 273
    Add-Check 'repeat and ending unchanged' $repeatOK 'Expected [47,296), count2, first ending273.'
    $expectedSeconds = 125760.0 * 60.0 / (480.0 * 58.0)
    $durationOK = [math]::Abs([double]$data.durationSeconds - $expectedSeconds) -lt 0.000001
    Add-Check 'expanded timeline duration unchanged' $durationOK `
        "seconds=$($data.durationSeconds) expected=$expectedSeconds (125760 ticks)"
} catch {
    Add-Check 'regression execution' $false $_.Exception.Message
}

$failed = @($checks | Where-Object { -not $_.passed }).Count
$passed = $checks.Count - $failed
$exitCode = if ($failed -eq 0) { 0 } else { 1 }
$last = "RESULT label=$Label passed=$passed failed=$failed exit=$exitCode"
$lines.Add($last)
Write-Output $last
$summary = [ordered]@{
    label = $Label
    command = $command
    executableSha256 = $exeHash
    imageSha256 = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
    recognizerExitCode = $processExit
    report = $savedReport
    stdout = $stdout
    stderr = $stderr
    passed = $passed
    failed = $failed
    exitCode = $exitCode
    checks = @($checks.ToArray())
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$lines | Set-Content -LiteralPath $logPath -Encoding utf8
exit $exitCode

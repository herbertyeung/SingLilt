# Verse lyric, instrument, and persistence regressions.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Executable = '',
    [string]$OutputDirectory = '',
    [string]$FixtureDirectory = ''
)

# Focused A/B integration checks through the shipping executable. No UI process
# is touched; small fixtures explicitly exercise persistence, not OCR shortcuts.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $root 'build/ab-verse-change/integration'
} elseif ([IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $root $OutputDirectory
}
$out = [IO.Path]::GetFullPath($out)
$buildPrefix = [IO.Path]::GetFullPath((Join-Path $root 'build')) + [IO.Path]::DirectorySeparatorChar
if (-not $out.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must be a subdirectory of the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $root "build/bin/$Configuration/SingLilt.exe"
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
if (-not $FixtureDirectory) { $FixtureDirectory = Join-Path $root 'build/private-fixtures' }
$image = (Resolve-Path -LiteralPath (Join-Path $FixtureDirectory 'buxia.png')).Path
$checks = New-Object 'System.Collections.Generic.List[object]'
$runs = New-Object 'System.Collections.Generic.List[object]'
$lines = New-Object 'System.Collections.Generic.List[string]'

function Add-Check([string]$Name, [bool]$Passed, [string]$Detail) {
    $checks.Add([pscustomobject]@{ name = $Name; passed = $Passed; detail = $Detail })
    $state = if ($Passed) { 'PASS' } else { 'FAIL' }
    $line = "$state $Name : $Detail"
    $lines.Add($line)
    Write-Host $line
}

function Invoke-Player([string]$Name, [string[]]$CliArguments, [int]$ExpectedExit = 0) {
    $report = Join-Path $out "$Name-report.json"
    $stdout = Join-Path $out "$Name-stdout.txt"
    $stderr = Join-Path $out "$Name-stderr.txt"
    $fullArguments = @($CliArguments) + @('--report', $report)
    $quoted = ($fullArguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $process = Start-Process -FilePath $Executable -ArgumentList $quoted -WindowStyle Hidden `
        -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $runs.Add([pscustomobject]@{
        name = $Name
        command = '"' + $Executable + '" ' + $quoted
        exitCode = $process.ExitCode
        expectedExitCode = $ExpectedExit
        report = $report
        stdout = $stdout
        stderr = $stderr
    })
    Add-Check "$Name process" ($process.ExitCode -eq $ExpectedExit) "exit=$($process.ExitCode) expected=$ExpectedExit"
    if (-not (Test-Path -LiteralPath $report)) { throw "$Name did not produce its report." }
    return (Get-Content -LiteralPath $report -Raw | ConvertFrom-Json)
}

function Write-Fixture([string]$Name, $Object) {
    $path = Join-Path $out "$Name.jpp"
    [IO.File]::WriteAllText($path, ($Object | ConvertTo-Json -Depth 12), [Text.UTF8Encoding]::new($false))
    return $path
}

function Same-Json($Left, $Right) {
    return ($Left | ConvertTo-Json -Depth 15 -Compress) -ceq ($Right | ConvertTo-Json -Depth 15 -Compress)
}

try {
    $projectPath = Join-Path $out 'recognized-schema4.jpp'
    $recognized = Invoke-Player 'recognize' @('--recognize', $image, '--out', $projectPath, '--timeline')
    $restored = Invoke-Player 'roundtrip' @('--inspect', $projectPath, '--timeline')
    $stream = [IO.File]::OpenRead($projectPath)
    try {
        $reader = [IO.BinaryReader]::new($stream)
        $magic = [Text.Encoding]::ASCII.GetString($reader.ReadBytes(8))
        if ($magic -ne "JPP4`r`n$([char]26)`n") { throw 'Saved file is not a version4 container.' }
        $sizeBytes = $reader.ReadBytes(4)
        [Array]::Reverse($sizeBytes)
        $size = [BitConverter]::ToUInt32($sizeBytes, 0)
        if ($size -gt 16MB) { throw 'Project manifest is oversized.' }
        $saved = [Text.Encoding]::UTF8.GetString($reader.ReadBytes($size)) | ConvertFrom-Json
        if ($saved.resources[0].role -ne 'image') { throw 'Expected first notation resource.' }
        $imageBytes = $reader.ReadBytes([int]$saved.resources[0].size)
        $saved | Add-Member -NotePropertyName imagePng -NotePropertyValue ([Convert]::ToBase64String($imageBytes))
    } finally { $stream.Dispose() }
    Add-Check 'schema4 package and legacy practice defaults' `
        ($saved.schemaVersion -eq 4 -and (Same-Json $saved.versePrograms @(0, 4)) -and
         $saved.practiceMix.melodyEnabled -and -not $saved.practiceMix.accompanimentEnabled -and
         -not $saved.PSObject.Properties['accompaniment']) `
        "schema=$($saved.schemaVersion) programs=$($saved.versePrograms -join ',')"
    Add-Check 'original source and expanded event counts' `
        ($recognized.noteCount -eq 393 -and $recognized.notes.Count -eq 393 -and $recognized.events.Count -eq 619) `
        "notes=$($recognized.notes.Count) events=$($recognized.events.Count) expected=393/619"
    $ticks = ($recognized.events | Measure-Object durationTicks -Sum).Sum
    Add-Check 'original timing and meter unchanged' `
        ($ticks -eq 125760 -and $restored.durationTicks -eq 125760 -and $recognized.bpm -eq 58 -and
         $recognized.tonic -eq 1 -and $recognized.beatsPerBar -eq 4 -and $recognized.beatUnit -eq 4) `
        "ticks=$ticks expected=125760 bpm=$($recognized.bpm) tonic=$($recognized.tonic) meter=$($recognized.beatsPerBar)/$($recognized.beatUnit)"
    Add-Check 'recognized and restored timelines valid' `
        ($recognized.validTimeline -and $restored.valid) "recognized=$($recognized.validTimeline) restored=$($restored.valid)"
    $sameScore = $true
    foreach ($field in @('title', 'tonic', 'bpm', 'beatsPerBar', 'beatUnit', 'notes', 'repeats', 'versePrograms')) {
        if (-not (Same-Json $recognized.$field $restored.score.$field)) { $sameScore = $false }
    }
    Add-Check 'schema4 score roundtrip' $sameScore 'All serialized score fields, verse slots and programs preserved.'
    Add-Check 'expanded event roundtrip' (Same-Json $recognized.events $restored.events) `
        'Source anchors, ticks, pitches, attacks, verse indexes, programs and lyrics preserved.'

    $firstVerse = '衣襟上别好了晚霞'.ToCharArray()
    $secondVerse = '过三巡酒气开月华'.ToCharArray()
    foreach ($offset in 0..7) {
        $index = 48 + $offset
        $a = @($recognized.events | Where-Object { $_.sourceNoteIndex -eq $index -and $_.verseIndex -eq 0 })
        $b = @($recognized.events | Where-Object { $_.sourceNoteIndex -eq $index -and $_.verseIndex -eq 1 })
        $note = $recognized.notes[$index]
        $expectedA = [string]$firstVerse[$offset]
        $expectedB = [string]$secondVerse[$offset]
        $ok = $a.Count -eq 1 -and $b.Count -eq 1 -and $a[0].lyric -ceq $expectedA -and
            $b[0].lyric -ceq $expectedB -and $a[0].program -eq 0 -and $b[0].program -eq 4 -and
            $a[0].midiPitch -eq $b[0].midiPitch -and $a[0].durationTicks -eq $b[0].durationTicks -and
            (Same-Json $note.verseLyrics @($expectedA, $expectedB))
        Add-Check "source $index A/B lyric and timbre" $ok `
            "A=$($a[0].lyric)/$expectedA program=$($a[0].program)/0 B=$($b[0].lyric)/$expectedB program=$($b[0].program)/4"
    }
    $firstEnding = $recognized.notes[273]
    $firstEndingEvents = @($recognized.events | Where-Object { $_.sourceNoteIndex -eq 273 })
    Add-Check 'first-ending blank B is explicit' `
        ((Same-Json $firstEnding.verseLyrics @('相', '')) -and $firstEndingEvents.Count -eq 1 -and
         $firstEndingEvents[0].verseIndex -eq 0 -and $firstEndingEvents[0].lyric -ceq '相') `
        'Source273 keeps [相, empty]; first ending appears only on A, never replayed as B.'
    $tail = @($recognized.events | Where-Object { $_.sourceNoteIndex -eq 303 })
    Add-Check 'single-line tail lyric serves B' `
        ((Same-Json $recognized.notes[303].verseLyrics @('当')) -and $tail.Count -eq 1 -and
         $tail[0].verseIndex -eq 1 -and $tail[0].program -eq 4 -and $tail[0].lyric -ceq '当') `
        "source303 verse=$($tail[0].verseIndex) program=$($tail[0].program) lyric=$($tail[0].lyric)"
    $bass = @($recognized.events | Where-Object { $_.sourceNoteIndex -eq 62 })
    Add-Check 'low-dot regression retained' `
        ($recognized.notes[62].octave -eq -1 -and $bass.Count -eq 2 -and
         @($bass | Where-Object { $_.midiPitch -ne 56 -or $_.durationTicks -ne 240 }).Count -eq 0) `
        'Source62 remains octave-1, MIDI56, duration240 in both passes.'

    # A deliberately small legacy score: newline A/B, blank B, blank A, shared
    # repeated lyric and a shared tail. Coordinates are only fixture metadata.
    $legacyTexts = @("A0`r`nB0", 'A1/', '/B2', 'shared', 'tail')
    $fixtureNotes = @()
    foreach ($index in 0..4) {
        $fixtureNotes += [ordered]@{
            id = $index; degree = $index + 1; octave = 0; accidental = 0; durationTicks = 480
            measure = [int][math]::Floor($index / 4); line = 0; lyric = $legacyTexts[$index]
            confidence = 1.0; keyOverride = -1; tieToNext = $false; bbox = @((30 + 20 * $index), 173, 10, 14)
        }
    }
    $legacy = [ordered]@{
        schemaVersion = 1; title = 'AB integration fixture: legacy slots'; tonic = 0; bpm = 90
        beatsPerBar = 4; beatUnit = 4; notes = $fixtureNotes
        repeats = @([ordered]@{ firstNote = 0; endNote = 4; count = 2; firstEndingNote = -1 })
        imagePng = $saved.imagePng; warnings = @('Synthetic five-note persistence fixture; not an OCR reference.')
    }
    $legacyPath = Write-Fixture 'legacy-v1' $legacy
    $migrated = Invoke-Player 'legacy-v1' @('--inspect', $legacyPath, '--timeline')
    $expectedSlots = @(@('A0', 'B0'), @('A1', ''), @('', 'B2'), @('shared'), @('tail'))
    $slotsOK = $migrated.notes -eq 5 -and $migrated.events.Count -eq 9
    foreach ($index in 0..4) {
        if (-not (Same-Json $migrated.score.notes[$index].verseLyrics $expectedSlots[$index])) { $slotsOK = $false }
    }
    Add-Check 'schema1 newline/slash migration' $slotsOK 'CRLF, blank A, blank B and singleton slots preserved.'
    $blankB = @($migrated.events | Where-Object { $_.sourceNoteIndex -eq 1 -and $_.verseIndex -eq 1 })
    $blankA = @($migrated.events | Where-Object { $_.sourceNoteIndex -eq 2 -and $_.verseIndex -eq 0 })
    Add-Check 'empty slots never fall back or shift' `
        ($blankB.Count -eq 1 -and $blankB[0].lyric -ceq '' -and $blankA.Count -eq 1 -and $blankA[0].lyric -ceq '') `
        'B of A1/ stays empty; A of /B2 stays empty.'
    $sharedB = @($migrated.events | Where-Object { $_.sourceNoteIndex -eq 3 -and $_.verseIndex -eq 1 })
    $sharedTail = @($migrated.events | Where-Object { $_.sourceNoteIndex -eq 4 })
    Add-Check 'singleton legacy lyric shared on B' `
        ($sharedB.Count -eq 1 -and $sharedB[0].lyric -ceq 'shared' -and
         $sharedTail.Count -eq 1 -and $sharedTail[0].verseIndex -eq 1 -and $sharedTail[0].lyric -ceq 'tail') `
        'Shared repeated note and post-repeat tail both supply B lyrics.'

    $custom = $legacy | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $custom.schemaVersion = 2
    $custom | Add-Member -NotePropertyName versePrograms -NotePropertyValue @(24, 73)
    foreach ($index in 0..4) {
        $custom.notes[$index] | Add-Member -NotePropertyName verseLyrics -NotePropertyValue $expectedSlots[$index]
    }
    $customPath = Write-Fixture 'custom-programs-v2' $custom
    $customRead = Invoke-Player 'custom-programs-v2' @('--inspect', $customPath, '--timeline')
    $wrongPrograms = @($customRead.events | Where-Object {
        ($_.verseIndex -eq 0 -and $_.program -ne 24) -or ($_.verseIndex -eq 1 -and $_.program -ne 73)
    })
    Add-Check 'custom programs preserved and expanded' `
        ((Same-Json $customRead.score.versePrograms @(24, 73)) -and $customRead.events.Count -eq 9 -and $wrongPrograms.Count -eq 0) `
        'Schema2 retains A=24, B=73; both passes and B tail use configured programs.'
    Add-Check 'explicit blank slots survive schema2' `
        ((Same-Json $customRead.score.notes[1].verseLyrics @('A1', '')) -and
         (Same-Json $customRead.score.notes[2].verseLyrics @('', 'B2'))) `
        'Explicit empty B/A entries remain in their original positions.'

    $invalidProgram = $custom | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $invalidProgram.versePrograms[1] = 128
    $invalidProgramPath = Write-Fixture 'invalid-program' $invalidProgram
    $badProgram = Invoke-Player 'invalid-program' @('--inspect', $invalidProgramPath, '--timeline') 1
    Add-Check 'invalid program diagnostic' ([string]$badProgram.error -match 'MIDI integer') ([string]$badProgram.error)
    $invalidVerse = $custom | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $invalidVerse.notes[0].verseLyrics = @('A0', 17)
    $invalidVersePath = Write-Fixture 'invalid-verse-item' $invalidVerse
    $badVerse = Invoke-Player 'invalid-verse-item' @('--inspect', $invalidVersePath, '--timeline') 1
    Add-Check 'invalid verse item diagnostic' ([string]$badVerse.error -match 'verse.*text') ([string]$badVerse.error)
    $invalidVerse.notes[0].verseLyrics = 'not-an-array'
    $invalidArrayPath = Write-Fixture 'invalid-verse-container' $invalidVerse
    $badArray = Invoke-Player 'invalid-verse-container' @('--inspect', $invalidArrayPath, '--timeline') 1
    Add-Check 'invalid verse container diagnostic' ([string]$badArray.error -match 'verseLyrics must be an array') ([string]$badArray.error)
} catch {
    Add-Check 'integration execution' $false $_.Exception.Message
}

$failed = @($checks | Where-Object { -not $_.passed }).Count
$passed = $checks.Count - $failed
$exitCode = if ($failed -eq 0) { 0 } else { 1 }
$last = "RESULT verse-integration passed=$passed failed=$failed exit=$exitCode"
$lines.Add($last)
Write-Host $last
$summary = [ordered]@{
    executable = $Executable
    executableSha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
    inputImage = $image
    imageSha256 = (Get-FileHash -LiteralPath $image -Algorithm SHA256).Hash
    passed = $passed; failed = $failed; exitCode = $exitCode
    runs = @($runs.ToArray()); checks = @($checks.ToArray())
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $out 'summary.json') -Encoding utf8
$lines | Set-Content -LiteralPath (Join-Path $out 'results.txt') -Encoding utf8
exit $exitCode

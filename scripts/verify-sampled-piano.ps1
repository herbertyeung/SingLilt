# Sampled-piano rendering, dynamics, and transport checks.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$Executable = '',
    [string]$OutputDirectory = ''
)

# Offline integration only: real sampler WAVs, persistence and missing-bank errors.
# No GUI, audio device, registry change, new target, or audio-unit-test duplication.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $root 'build/piano-sampling/integration'
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
$applicationDirectory = Split-Path -Parent $Executable
$pianoPath = Join-Path $applicationDirectory 'assets/soundfonts/Salamander.sf2'
$introPath = Join-Path $root 'build/private-fixtures/buxia-intro.jpp'
$expectedPianoHash = '712D0E681EFBE5203A8014E9B3E84168F1908C82F2F6FB13BD2C77D6D72C70B7'
$checks = New-Object 'System.Collections.Generic.List[object]'
$runs = New-Object 'System.Collections.Generic.List[object]'
$waves = New-Object 'System.Collections.Generic.List[object]'
$lines = New-Object 'System.Collections.Generic.List[string]'
$previousSoundFont = [Environment]::GetEnvironmentVariable('JIANPU_SOUNDFONT', 'Process')
$actualPianoHash = $null

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
    $arguments = @($CliArguments) + @('--report', $report)
    $quoted = ($arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $process = Start-Process -FilePath $Executable -ArgumentList $quoted -WindowStyle Hidden `
        -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $runs.Add([pscustomobject]@{
        name = $Name; command = '"' + $Executable + '" ' + $quoted
        soundFontEnvironment = [Environment]::GetEnvironmentVariable('JIANPU_SOUNDFONT', 'Process')
        exitCode = $process.ExitCode; expectedExitCode = $ExpectedExit
        report = $report; stdout = $stdout; stderr = $stderr
    })
    Add-Check "$Name process" ($process.ExitCode -eq $ExpectedExit) "exit=$($process.ExitCode) expected=$ExpectedExit"
    if (-not (Test-Path -LiteralPath $report)) { throw "$Name did not produce its report." }
    return (Get-Content -LiteralPath $report -Raw | ConvertFrom-Json)
}

function Inspect-Wave([string]$Path) {
    $file = [IO.File]::OpenRead($Path)
    try {
        $header = New-Object byte[] 44
        if ($file.Read($header, 0, 44) -ne 44) { throw 'WAV header is incomplete.' }
        $ascii = [Text.Encoding]::ASCII
        $info = [ordered]@{
            path = $Path; bytes = $file.Length
            riff = $ascii.GetString($header, 0, 4); riffBytes = [BitConverter]::ToUInt32($header, 4)
            wave = $ascii.GetString($header, 8, 4); formatChunk = $ascii.GetString($header, 12, 4)
            formatBytes = [BitConverter]::ToUInt32($header, 16); format = [BitConverter]::ToUInt16($header, 20)
            channels = [BitConverter]::ToUInt16($header, 22); sampleRate = [BitConverter]::ToUInt32($header, 24)
            byteRate = [BitConverter]::ToUInt32($header, 28); blockAlign = [BitConverter]::ToUInt16($header, 32)
            bitsPerSample = [BitConverter]::ToUInt16($header, 34); dataChunk = $ascii.GetString($header, 36, 4)
            dataBytes = [BitConverter]::ToUInt32($header, 40)
        }
        # Independent PCM measurements, streamed with PowerShell/.NET binary I/O.
        $buffer = New-Object byte[] 65536
        [long]$sampleCount = 0
        [long]$saturated = 0
        [double]$squareSum = 0
        [double]$peak = 0
        while (($read = $file.Read($buffer, 0, $buffer.Length)) -gt 0) {
            if (($read % 2) -ne 0) { throw 'PCM sample has an incomplete trailing byte.' }
            for ($i = 0; $i -lt $read; $i += 2) {
                $pcm = [BitConverter]::ToInt16($buffer, $i)
                $value = $pcm / 32768.0
                $squareSum += $value * $value
                $peak = [math]::Max($peak, [math]::Abs($value))
                if ($pcm -eq 32767 -or $pcm -eq -32768) { ++$saturated }
                ++$sampleCount
            }
        }
        $info.frames = $sampleCount / 2
        $info.pcmPeak = $peak
        $info.pcmRms = if ($sampleCount -gt 0) { [math]::Sqrt($squareSum / $sampleCount) } else { 0 }
        $info.saturatedSamples = $saturated
        return [pscustomobject]$info
    } finally { $file.Dispose() }
}

function Write-Fixture([string]$Name, $Object) {
    $path = Join-Path $out "$Name.jpp"
    [IO.File]::WriteAllText($path, ($Object | ConvertTo-Json -Depth 14), [Text.UTF8Encoding]::new($false))
    return $path
}

try {
    # Explicit process-local selection makes the test independent of a user's
    # custom bank, while restoring that environment value even on failure.
    [Environment]::SetEnvironmentVariable('JIANPU_SOUNDFONT', $pianoPath, 'Process')
    $rendered = @{}
    $decoded = @{}
    foreach ($velocity in @(40, 110)) {
        $name = "piano-$velocity"
        $wavePath = Join-Path $out "$name.wav"
        $render = Invoke-Player $name @('--render-wave', $wavePath, '--render-seconds', '3',
            '--velocity', [string]$velocity, '--uniform', $introPath)
        $rendered[$velocity] = $render
        Add-Check "$name raw sampled output" `
            ($render.frames -eq 216000 -and $render.sampleRate -eq 48000 -and $render.peak -gt 0 -and
             $render.rms -gt 0 -and $render.clippedSamples -eq 0 -and $render.baseVelocity -eq $velocity -and
             $render.accentBeats -eq $false) `
            "frames=$($render.frames) peak=$($render.peak) rms=$($render.rms) clipped=$($render.clippedSamples) velocity=$($render.baseVelocity) accents=$($render.accentBeats)"
        $reportedPiano = [IO.Path]::GetFullPath([string]$render.pianoPath)
        Add-Check "$name real sampler provenance" `
            ($render.engine -match 'FluidSynth' -and $render.engine -match 'Salamander' -and
             $reportedPiano.Equals([IO.Path]::GetFullPath($pianoPath), [StringComparison]::OrdinalIgnoreCase)) `
            "engine=$($render.engine) pianoPath=$reportedPiano"
        $wav = Inspect-Wave $wavePath
        $wav | Add-Member -NotePropertyName sha256 -NotePropertyValue (Get-FileHash -LiteralPath $wavePath -Algorithm SHA256).Hash
        $decoded[$velocity] = $wav
        $waves.Add($wav)
        $headerOK = $wav.riff -ceq 'RIFF' -and $wav.wave -ceq 'WAVE' -and $wav.formatChunk -ceq 'fmt ' -and
            $wav.formatBytes -eq 16 -and $wav.format -eq 1 -and $wav.channels -eq 2 -and
            $wav.sampleRate -eq 48000 -and $wav.byteRate -eq 192000 -and $wav.blockAlign -eq 4 -and
            $wav.bitsPerSample -eq 16 -and $wav.dataChunk -ceq 'data' -and $wav.dataBytes -eq 864000 -and
            $wav.bytes -eq ($wav.dataBytes + 44) -and $wav.riffBytes -eq ($wav.bytes - 8) -and
            $wav.frames -eq $render.frames -and $wav.pcmRms -gt 0 -and $wav.saturatedSamples -eq 0
        Add-Check "$name WAV bytes and decoded PCM" $headerOK `
            "bytes=$($wav.bytes) frames=$($wav.frames) PCM16/stereo/rate=$($wav.sampleRate) decodedRms=$($wav.pcmRms) saturated=$($wav.saturatedSamples)"
    }
    Add-Check 'soft and hard timing identical' ($rendered[40].frames -eq $rendered[110].frames) `
        'Both contain 3 seconds of music plus the documented 1.5-second release tail.'
    Add-Check 'hard touch raises raw RMS' ($rendered[110].rms -gt $rendered[40].rms) `
        "soft=$($rendered[40].rms) hard=$($rendered[110].rms)"
    Add-Check 'hard touch raises independently decoded RMS' ($decoded[110].pcmRms -gt $decoded[40].pcmRms) `
        "soft=$($decoded[40].pcmRms) hard=$($decoded[110].pcmRms)"
    $actualPianoHash = (Get-FileHash -LiteralPath $pianoPath -Algorithm SHA256).Hash
    Add-Check 'pinned Salamander bank SHA256' ($actualPianoHash -ceq $expectedPianoHash) $actualPianoHash
    $metadataPath = Join-Path (Split-Path -Parent $pianoPath) 'metadata.json'
    if (Test-Path -LiteralPath $metadataPath) {
        $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        Add-Check 'optional bank manifest matches file' `
            ([string]$metadata.sha256 -ieq $actualPianoHash -and $metadata.velocity_layer_count -eq 16) `
            "sha256=$($metadata.sha256) layers=$($metadata.velocity_layer_count)"
    }

    $fixture = Get-Content -LiteralPath $introPath -Raw | ConvertFrom-Json
    $fixture.schemaVersion = 2
    $fixture | Add-Member -Force -NotePropertyName baseVelocity -NotePropertyValue 67
    $fixture | Add-Member -Force -NotePropertyName accentBeats -NotePropertyValue $false
    $fixturePath = Write-Fixture 'performance-settings' $fixture
    $inspection = Invoke-Player 'performance-settings' @('--inspect', $fixturePath, '--timeline')
    $wrongVelocity = @($inspection.events | Where-Object {
        ($_.midiPitch -ge 0 -and $_.velocity -ne 67) -or ($_.midiPitch -lt 0 -and $_.velocity -ne 0)
    })
    Add-Check 'stored performance controls drive events' `
        ($inspection.valid -and $inspection.score.baseVelocity -eq 67 -and
         $inspection.score.accentBeats -eq $false -and $wrongVelocity.Count -eq 0) `
        'baseVelocity67/accentBeats=false retained; sounded events use67 and rests0.'
    $serialized = $inspection.score
    $serialized | Add-Member -NotePropertyName schemaVersion -NotePropertyValue 2
    $serialized | Add-Member -NotePropertyName imagePng -NotePropertyValue $fixture.imagePng
    $serialized | Add-Member -NotePropertyName warnings -NotePropertyValue @()
    $roundtripPath = Write-Fixture 'performance-roundtrip' $serialized
    $roundtrip = Invoke-Player 'performance-roundtrip' @('--inspect', $roundtripPath, '--timeline')
    Add-Check 'performance fields serialize and reload' `
        ($roundtrip.score.baseVelocity -eq 67 -and $roundtrip.score.accentBeats -eq $false -and
         $roundtrip.durationTicks -eq $inspection.durationTicks -and
         ($roundtrip.events | ConvertTo-Json -Depth 10 -Compress) -ceq ($inspection.events | ConvertTo-Json -Depth 10 -Compress)) `
        'Actual scoreToJson output reloaded with identical performance events and timing.'

    $invalidWave = Join-Path $out ('invalid-velocity-' + [guid]::NewGuid().ToString('N') + '.wav')
    $invalidVelocity = Invoke-Player 'invalid-velocity' @('--render-wave', $invalidWave, '--render-seconds', '3',
        '--velocity', '0', $introPath) 1
    Add-Check 'invalid velocity rejects before audio output' `
        ([string]$invalidVelocity.error -match 'velocity.*1.*127' -and -not (Test-Path -LiteralPath $invalidWave)) `
        "error=$($invalidVelocity.error) outputExists=$(Test-Path -LiteralPath $invalidWave)"
    $fixture.accentBeats = 'false'
    $badAccentPath = Write-Fixture 'invalid-accent-type' $fixture
    $invalidAccent = Invoke-Player 'invalid-accent-type' @('--inspect', $badAccentPath, '--timeline') 1
    Add-Check 'string accent value rejected' ([string]$invalidAccent.error -match 'accentBeats must be boolean') `
        ([string]$invalidAccent.error)

    $missingBank = Join-Path $out ('missing-' + [guid]::NewGuid().ToString('N') + '.sf2')
    $missingWave = Join-Path $out ('missing-bank-' + [guid]::NewGuid().ToString('N') + '.wav')
    [Environment]::SetEnvironmentVariable('JIANPU_SOUNDFONT', $missingBank, 'Process')
    $missing = Invoke-Player 'missing-soundfont' @('--render-wave', $missingWave, '--render-seconds', '3', $introPath) 1
    Add-Check 'missing bank rejects without substitute audio' `
        ([string]$missing.error -match 'SoundFont is missing' -and -not (Test-Path -LiteralPath $missingWave)) `
        "error=$($missing.error) outputExists=$(Test-Path -LiteralPath $missingWave)"
} catch {
    Add-Check 'sampled piano integration execution' $false $_.Exception.Message
} finally {
    if ($null -eq $previousSoundFont) {
        # PowerShell binds null to an empty .NET string on this runtime. Remove
        # the process-provider entry explicitly to restore true absence.
        Remove-Item -LiteralPath Env:\JIANPU_SOUNDFONT -ErrorAction SilentlyContinue
    } else {
        [Environment]::SetEnvironmentVariable('JIANPU_SOUNDFONT', $previousSoundFont, 'Process')
    }
}
Add-Check 'process SoundFont environment restored' `
    ([Environment]::GetEnvironmentVariable('JIANPU_SOUNDFONT', 'Process') -ceq $previousSoundFont) `
    'Original process-local value restored; user and machine environments untouched.'
$failed = @($checks | Where-Object { -not $_.passed }).Count
$passed = $checks.Count - $failed
$exitCode = if ($failed -eq 0) { 0 } else { 1 }
$last = "RESULT sampled-piano-integration passed=$passed failed=$failed exit=$exitCode"
$lines.Add($last)
Write-Host $last
$summary = [ordered]@{
    executable = $Executable; executableSha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
    pianoPath = $pianoPath; pianoSha256 = $actualPianoHash; expectedPianoSha256 = $expectedPianoHash
    musicalSeconds = 3; releaseTailSeconds = 1.5
    passed = $passed; failed = $failed; exitCode = $exitCode
    runs = @($runs.ToArray()); waves = @($waves.ToArray()); checks = @($checks.ToArray())
}
$summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $out 'summary.json') -Encoding utf8
$lines | Set-Content -LiteralPath (Join-Path $out 'results.txt') -Encoding utf8
exit $exitCode

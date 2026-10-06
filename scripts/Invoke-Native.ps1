# Checked child-process execution with a normalized Windows environment.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [string]$WorkingDirectory = (Split-Path -Parent $PSScriptRoot)
    )
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $FilePath
    $info.WorkingDirectory = $WorkingDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $ArgumentList) {
        [void]$info.ArgumentList.Add($argument)
    }
    # MSBuild rejects inherited Path/PATH duplicates. Normalize the child only.
    $environment = [Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $environment[[string]$entry.Key] = [string]$entry.Value
    }
    $info.Environment.Clear()
    foreach ($entry in $environment.GetEnumerator()) {
        $info.Environment[$entry.Key] = $entry.Value
    }
    $process = [Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        if ($stdout.Result) { Write-Output $stdout.Result.TrimEnd() }
        if ($stderr.Result) { [Console]::Error.WriteLine($stderr.Result.TrimEnd()) }
        if ($process.ExitCode -ne 0) {
            throw "$FilePath exited with $($process.ExitCode)"
        }
    } finally {
        $process.Dispose()
    }
}

#requires -Version 5.1
$ErrorActionPreference='Stop'

function Resolve-ConsoleFilePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { throw 'File path must not be empty.' }
    $provider=$null; $drive=$null
    $resolved=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path,[ref]$provider,[ref]$drive)
    if ($provider.Name -ne 'FileSystem') { throw "A filesystem path is required: $Path" }
    return $resolved
}

function New-RecordingPlan([string]$RecordPath,[string]$ParsedLog,[string]$InputFile,[switch]$Force,[scriptblock]$Confirm) {
    $outputs=[Collections.Generic.List[object]]::new()
    foreach ($name in @('RecordPath','ParsedLog')) {
        $path=if ($name -eq 'RecordPath') {$RecordPath} else {$ParsedLog}
        if (!$path) { continue }
        $path=Resolve-ConsoleFilePath $path
        if ($InputFile -and $path -eq (Resolve-ConsoleFilePath $InputFile)) { throw 'Output cannot replace input, even with -Force.' }
        if (@($outputs | Where-Object {$_.Path -eq $path}).Count) { throw 'Record and parsed log must be different files, even with -Force.' }
        if ([IO.Directory]::Exists($path)) { throw "Output is a directory: $path" }
        $outputs.Add([pscustomobject]@{Name=$name;Path=$path;Overwrite=[IO.File]::Exists($path)})
    }
    foreach ($output in $outputs) {
        if (!$output.Overwrite -or $Force) { continue }
        if (!$Confirm) {
            if ([Console]::IsInputRedirected -or ![Environment]::UserInteractive) { throw "Output already exists: $($output.Path). Use -Force to overwrite unattended." }
            $Confirm={param($Path) (Read-Host "Overwrite existing output '$Path'? [y/N]") -match '^(?i)y(?:es)?$'}
        }
        if (!(& $Confirm $output.Path)) { throw "Overwrite cancelled; existing outputs were left untouched: $($output.Path)" }
    }
    return $outputs.ToArray()
}

function Open-RecordingOutputs([object[]]$Plan,[string]$InputFile) {
    $streams=@{}; $writers=@{}; $inputLock=$null
    try {
        # Keep replay input read-only even if an output is a hard-link alias.
        if ($InputFile) { $inputLock=[IO.File]::Open($InputFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read) }
        foreach ($output in $Plan) {
            $null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output.Path))
            $mode=if ($output.Overwrite) {[IO.FileMode]::Open} else {[IO.FileMode]::CreateNew}
            $streams[$output.Name]=[IO.File]::Open($output.Path,$mode,[IO.FileAccess]::Write,[IO.FileShare]::Read)
        }
        # All paths, confirmations and writable handles are checked before truncation.
        foreach ($output in $Plan) { $streams[$output.Name].SetLength(0) }
        foreach ($output in $Plan) {
            $writer=[IO.StreamWriter]::new($streams[$output.Name],[Text.UTF8Encoding]::new($false))
            $writer.AutoFlush=$true; $writers[$output.Name]=$writer
        }
        return [pscustomobject]@{Writers=$writers;InputLock=$inputLock}
    } catch {
        foreach ($writer in $writers.Values) { $writer.Dispose() }
        foreach ($stream in $streams.Values) { $stream.Dispose() }
        if ($inputLock) { $inputLock.Dispose() }
        throw
    }
}

function New-RecordingProgress([string]$Path,[string]$InputFormat='Readable',[double]$RecordTime=0,[long]$MaxRecordItems=0) {
    [pscustomobject]@{Path=$Path;InputFormat=$InputFormat;Lines=0L;Events=0L;Clock=[Diagnostics.Stopwatch]::StartNew();LastUpdate=-500L;Suspended=$false;Bytes=0L;RecordTime=$RecordTime;MaxRecordItems=$MaxRecordItems;LimitClock=[Diagnostics.Stopwatch]::new();StopReason=$null;StopHint='Press Q to stop recording'}
}

function Start-RecordingLimits($Progress) {
    if ($Progress) { $Progress.LimitClock.Start() }
}

function Get-RecordingStopReason($Progress) {
    if (!$Progress) { return $null }
    if ($Progress.StopReason) { return $Progress.StopReason }
    if ($Progress.RecordTime -gt 0 -and $Progress.LimitClock.Elapsed.TotalSeconds -ge $Progress.RecordTime) {
        $Progress.StopReason="RecordTime reached ($($Progress.RecordTime) seconds)"
    } elseif ($Progress.MaxRecordItems -gt 0 -and $Progress.Events -ge $Progress.MaxRecordItems) {
        $Progress.StopReason="MaxRecordItems reached ($($Progress.MaxRecordItems) events)"
    }
    return $Progress.StopReason
}

function Add-RecordingEvent($Progress,[string]$Line) {
    $Progress.Lines++
    if (($Progress.InputFormat -eq 'Raw' -and $Line -match '^RAW,') -or
        ($Progress.InputFormat -eq 'Readable' -and $Line -match '^(0x|Undecodable)')) { $Progress.Events++ }
}

function Format-RecordingDuration([double]$Seconds) {
    $whole=[long][Math]::Floor([Math]::Max(0,$Seconds))
    '{0:00}:{1:00}:{2:00}' -f [long][Math]::Floor($whole/3600),[long][Math]::Floor(($whole%3600)/60),($whole%60)
}

function Update-RecordingProgress($Progress,$Writer,[switch]$Force,[switch]$Completed,[scriptblock]$Render) {
    if (!$Progress -or !$Writer) { return }
    if (!$Render) { $Render={param($Parameters) Write-Progress @Parameters} }
    if ($Completed) { & $Render @{Id=2366;Activity='Recording I2C';Completed=$true}; return }
    if ($Progress.Suspended) { return }
    $now=$Progress.Clock.ElapsedMilliseconds
    if (!$Force -and $now-$Progress.LastUpdate -lt 500) { return }
    $Progress.LastUpdate=$now
    $Progress.Bytes=$Writer.BaseStream.Length
    $size=('{0:N1} KiB ({1} bytes)' -f ($Progress.Bytes/1KB),$Progress.Bytes)
    $kind=if ($Progress.InputFormat -eq 'Raw') {'RAW frames'} else {'transactions'}
    # Use the same clock as RecordTime, excluding setup but including pauses.
    $elapsedSeconds=$Progress.LimitClock.Elapsed.TotalSeconds
    $elapsed=Format-RecordingDuration $elapsedSeconds
    $activity="Recording I2C - elapsed $elapsed"
    $parameters=@{Id=2366;Activity=$activity;Status="$($Progress.Events) $kind; $($Progress.Lines) recorded lines; $size; $($Progress.StopHint)";CurrentOperation=$Progress.Path;PercentComplete=-1}
    if ($Progress.RecordTime -gt 0) {
        $duration=Format-RecordingDuration $Progress.RecordTime
        $remainingSeconds=[Math]::Max(0,$Progress.RecordTime-$elapsedSeconds)
        $remaining=Format-RecordingDuration ([Math]::Ceiling($remainingSeconds))
        $parameters.Activity="$activity / $duration (remaining $remaining)"
        $parameters.PercentComplete=[int][Math]::Min(100,[Math]::Floor(100*$elapsedSeconds/$Progress.RecordTime))
        $parameters.SecondsRemaining=[int][Math]::Ceiling($remainingSeconds)
    }
    & $Render $parameters
}

Export-ModuleMember -Function Resolve-ConsoleFilePath,New-RecordingPlan,Open-RecordingOutputs,New-RecordingProgress,Add-RecordingEvent,Update-RecordingProgress,Start-RecordingLimits,Get-RecordingStopReason

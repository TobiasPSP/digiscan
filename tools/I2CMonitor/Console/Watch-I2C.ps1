#requires -Version 5.1
[CmdletBinding(PositionalBinding=$false)]
param(
    [string]$Port = $null,
    [string]$InputFile = $null,
    [switch]$ListPorts,
    [switch]$ValidateProfile,
    [string]$Profile = $null,
    [ValidateSet('Readable','Raw')][string]$InputFormat='Readable',
    [ValidateRange(9600,921600)][int]$Baud=115200,
    [string]$RecordPath,
    [ValidateRange(0,2147483647)][int]$RecordTime=0,
    [ValidateRange(0,9223372036854775807)][long]$MaxRecordItems=0,
    [string]$ParsedLog,
    [ValidateRange(0,86400)][int]$MaxSeconds=0,
    [switch]$NoColor,
    [string]$InvestigateAddress = $null,
    [string]$InvestigateRegisters = $null,
    [switch]$ShowInventory,
    [switch]$ShowFailures,
    [switch]$ShowRemark,
    [switch]$Force,
    [ValidateSet('All','Known','Unknown')][string]$View='All',
    [ValidateSet('Both','Success','Failure')][string]$Outcome='Both',
    [ValidateSet('','On','Off')][string]$Qualification=$null
)
$ErrorActionPreference='Stop'
if (($PSBoundParameters.ContainsKey('RecordTime') -and $RecordTime -eq 0) -or
    ($PSBoundParameters.ContainsKey('MaxRecordItems') -and $MaxRecordItems -eq 0)) { throw 'Recording limits must be positive when specified; omit them for unlimited recording.' }
if (!$Profile) { $Profile = (Join-Path $PSScriptRoot 'profiles/ip2366.yaml') }
if ($Port -and $Port -notmatch '^COM[1-9]\d*$') { throw 'Port must be a COM name, for example COM7.' }
if ($InputFile -and [string]::IsNullOrWhiteSpace($InputFile)) { throw 'InputFile must be a filename or omitted.' }
if ($Port -and $InputFile) { throw 'Port selects live mode; InputFile selects replay. Specify only one.' }
if ($ListPorts -and $ValidateProfile) { throw 'Choose either ListPorts or ValidateProfile.' }
if (($ListPorts -or $ValidateProfile) -and ($Port -or $InputFile -or $RecordPath -or $ParsedLog -or $InvestigateAddress -or $InvestigateRegisters -or $ShowInventory -or $ShowFailures)) {
    throw 'ListPorts and ValidateProfile are standalone actions; omit capture/replay and log parameters.'
}
$mode = if ($ListPorts) { 'List' } elseif ($ValidateProfile) { 'Validate' } elseif ($InputFile) { 'Replay' } else { 'Live' }
if (($RecordTime -or $MaxRecordItems) -and !$RecordPath) { throw 'RecordTime and MaxRecordItems require RecordPath.' }
Import-Module (Join-Path $PSScriptRoot 'Interpreter.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'SerialPorts.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Investigation.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'FailureDiagnostics.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'FirmwareProtocol.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Recording.psm1') -Force
$firmware=New-FirmwareSession
$firmware.Replay=$mode -eq 'Replay'
if ($mode -eq 'List') {
    $ports=@(Get-SnifferPorts)
    if ($ports.Count) { $ports | Format-Table Port,Description,LikelySniffer -AutoSize }
    else { Write-Host 'No COM ports found. Connect the RP2040 running the sniffer and retry.' }
    return
}
$Profile=Resolve-ConsoleFilePath $Profile
$definition=Import-DeviceProfile $Profile
if ($mode -eq 'Validate') { Write-Host "Valid profile: $($definition.Source.device.name), $($definition.Properties.Count) properties"; return }
$session=[pscustomobject]@{Inventory=(New-RegisterInventory);Failures=(New-FailureDiagnostics);Selection=$null;Events=[Collections.Generic.List[object]]::new();IdleSeen=$false}
if ($InvestigateAddress -and !$InvestigateRegisters) { throw 'InvestigateAddress requires InvestigateRegisters.' }
if ($InvestigateRegisters) {
    $address=$definition.Address
    if ($InvestigateAddress) {
        if ($InvestigateAddress -notmatch '^(?:0x)?([0-9A-Fa-f]{1,2})$') { throw 'InvestigateAddress must be a hexadecimal 7-bit address, e.g. 0x75.' }
        $address=[Convert]::ToInt32($Matches[1],16)
    }
    $session.Selection=New-InvestigationSelection $address @(Convert-RegisterSelection $InvestigateRegisters)
}
if ($InputFile) { $InputFile=Resolve-ConsoleFilePath $InputFile }
$outputPlan=@(New-RecordingPlan -RecordPath $RecordPath -ParsedLog $ParsedLog -InputFile $InputFile -Force:$Force)
foreach ($output in $outputPlan) { if ($output.Name -eq 'RecordPath') { $RecordPath=$output.Path } else { $ParsedLog=$output.Path } }
if ($mode -eq 'Live') { $Port=Select-SnifferPort -Ports @(Get-SnifferPorts) -RequestedPort $Port }
$state=New-InterpreterState $definition -ShowRemark:$ShowRemark
# A closure gets a separate dynamic module scope. Capture command identities
# explicitly instead of assuming imported sibling functions are globally visible.
$inventoryObserver=Get-Command Add-ObservedTransaction -ErrorAction Stop
$diagnosticObserver=Get-Command Add-RegisterDiagnostic -ErrorAction Stop
$state.TransactionObserver={
    param($Transaction)
    & $inventoryObserver $session.Inventory $definition $Transaction
    & $diagnosticObserver $session.Failures $Transaction
    $session.Events.Add($Transaction)
}.GetNewClosure()
$serial=$null; $record=$null; $parsed=$null; $recordingProgress=$null; $openedOutputs=$null
function Show-Result([string]$Line) {
    if ($parsed) { $parsed.WriteLine($Line) }
    if ($NoColor) { Write-Host $Line; return }
    $color=if ($Line -match 'FAIL|PARSER_ERROR|INPUT_GAP|OUTPUT_LOSS|ERROR') {'Red'} elseif ($Line -match 'INCOMPLETE|CONFLICT|unverified|OUT_OF_RANGE|UNDEFINED|UNPARSED') {'Yellow'} elseif ($Line -match ' OK') {'Green'} else {'Gray'}
    Write-Host $Line -ForegroundColor $color
}
function Process-Line([string]$Line) {
    if ($record -and !(Get-RecordingStopReason $recordingProgress)) {
        $record.WriteLine($Line)
        Add-RecordingEvent $recordingProgress $Line
        $null=Get-RecordingStopReason $recordingProgress
        Update-RecordingProgress $recordingProgress $record
    }
    if (Update-FirmwareSession $firmware $Line) {
        if ($Line -match '^REPORT_(BEGIN|END),kind=([DT])$') {
            $label=if ($Matches[2] -eq 'D') {'DIAGNOSTICS'} else {'TIMING'}
            Show-Result ("----- {0} {1} -----" -f $label,$Matches[1])
        } elseif ($Line -match '^(FAULT|TIMING|QUAL)') { Show-Result $Line }
        return
    }
    if ($Line -match '^IDLE(?:$|,)') { $session.IdleSeen=$true }
    $session.Events.Clear()
    if ($Line -match '^0x' -and $InputFormat -eq 'Readable' -and $firmware.PendingFault -and $firmware.PendingFault.association -eq 'unassociated') {
        Reset-InterpreterAssembly $state 'unassociated damaged transaction' | ForEach-Object { Show-Result $_ }
        $state.Transactions++; $state.Failures++
        $decoded=@("FAIL: $Line [register association untrusted]")
    } else { $decoded=@(Invoke-InputLine $state $Line $InputFormat) }
    if ($Line -match '^(0x|Undecodable)' -or $Line -match '^(CLEAR|IDLE|ARMED|INPUT_GAP|OUTPUT_LOSS|RP2040)') { $firmware.PendingFault=$null }
    Update-FailureDiagnosticInput $session.Failures $Line ($session.Events.Count -gt 0)
    foreach ($diagnostic in @($decoded | Where-Object { $_ -match '^(INPUT_GAP|PARSER_ERROR)' -and $_ -ne $Line })) {
        Update-FailureDiagnosticInput $session.Failures $diagnostic
    }
    $sourceFiltered=$mode -eq 'Live' -and $firmware.Capable -and $InputFormat -eq 'Readable' -and $null -ne $firmware.Applied
    if ($null -eq $session.Selection -and ($sourceFiltered -or ($View -eq 'All' -and $Outcome -eq 'Both'))) { $decoded | ForEach-Object { Show-Result $_ }; return }
    if ($session.Events.Count) {
        foreach ($transaction in $session.Events) {
            $matchesScope=$true
            if (!$sourceFiltered) {
                if ($null -ne $session.Selection) { $matchesScope=Test-InvestigationTransaction $session.Selection $transaction }
                elseif ($View -ne 'All') {
                    $maps=New-FilterMaps $definition $View
                    $reg=$transaction.Register
                    $matchesScope=$null -ne $reg -and $maps.ContainsKey([int]$transaction.Address) -and
                        ($maps[[int]$transaction.Address][[int][Math]::Floor($reg/8)] -band (1 -shl ($reg%8))) -ne 0
                }
            }
            $matchesOutcome=$sourceFiltered -or $Outcome -eq 'Both' -or ($Outcome -eq 'Success' -and $transaction.Ok) -or ($Outcome -eq 'Failure' -and !$transaction.Ok)
            if ($matchesScope -and $matchesOutcome) {
                if ($null -eq $session.Selection) { $decoded | ForEach-Object { Show-Result $_ }; break }
                $prefix=if ($transaction.Ok) { '' } else { 'FAIL: ' }
                Show-Result ($prefix + $transaction.Source)
            }
        }
    } else {
        # Keep control/loss diagnostics visible, but do not partially parse an
        # unsupported compound transfer to guess its register associations.
        if ($Line -notmatch '^(0x|RAW,|NEW,|EDGE,)') { $decoded | Where-Object { $_ -notmatch '^0x[0-9A-Fa-f]{2} R .*INCOMPLETE' } | ForEach-Object { Show-Result $_ } }
        elseif ($decoded.Count -and $null -eq $session.Selection -and $View -eq 'All' -and $Outcome -ne 'Success') { $decoded | ForEach-Object { Show-Result $_ } }
    }
}
function Show-InvestigationStatus {
    $location=if ($mode -eq 'Live' -and $firmware.Capable -and !$firmware.Applied) {'firmware setup FAILED; capture remains paused'}
        elseif ($mode -eq 'Live' -and $firmware.Capable -and $InputFormat -eq 'Readable') {'firmware'}
        else {'PC fallback/replay'}
    if ($null -eq $session.Selection) { Show-Result "VIEW $View / $Outcome; filtering: $location; normal YAML interpretation"; return }
    $regs=($session.Selection.Registers | ForEach-Object { '0x{0:X2}' -f $_ }) -join ','
    Show-Result ('VIEW raw investigation: address 0x{0:X2}, registers {1}; {2}; filtering: {3}.' -f $session.Selection.Address,$regs,$Outcome,$location)
}
function Wait-FirmwareResponse([long]$Id,[string]$Command,[int]$TimeoutMilliseconds=5000) {
    $wait=[Diagnostics.Stopwatch]::StartNew()
    while ($wait.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
        Convert-InputChunk $buffer $serial.ReadExisting() | ForEach-Object { Process-Line $_ }
        if ($firmware.Responses.ContainsKey("$Id")) {
            $response=$firmware.Responses["$Id"]
            if ($response.Kind -eq 'NAK') { throw "Firmware rejected $Command`: $($response.Fields.reason); generation=$($response.Fields.generation)" }
            if ($response.Kind -eq $Command -or $response.Fields.command -eq $Command) { return $response }
        }
        Start-Sleep -Milliseconds 10
    }
    throw "Firmware $Command acknowledgement timed out. Capture stays paused; applied filter is uncertain."
}
function Set-SourceFilter {
    if (!$firmware.Capable) { $firmware.Applied=$null; return }
    $firmware.Request++; $id=$firmware.Request
    $scope=if ($null -ne $session.Selection -or $View -ne 'All') {'Selected'} else {'All'}
    $maps=New-FilterMaps $definition $View $session.Selection
    $wireOutcome=$Outcome
    if ($InputFormat -eq 'Raw') { $scope='All'; $wireOutcome='Both'; $maps=@{} }
    try {
        foreach ($command in @(Get-FilterCommands $id $scope $wireOutcome $maps)) {
            $firmware.Responses.Remove("$id")
            $serial.Write($command+"`n")
            $name=($command.Substring(1).Split(','))[0]
            $response=Wait-FirmwareResponse $id $name
        }
        if ($response.Fields.scope -ne $scope -or $response.Fields.outcome -ne $wireOutcome) { throw 'Firmware acknowledged a different filter.' }
        if ($Qualification) { Set-Qualification $Qualification }
        $firmware.Applied=$response.Fields
        Reset-InterpreterAssembly $state 'filter configuration changed' | ForEach-Object { Show-Result $_ }
        return $true
    } catch {
        $firmware.Applied=$null
        Show-Result ('ERROR '+$_.Exception.Message+' No resume requested; retry a view selection before starting.')
        try {
            $firmware.Request++; $queryId=$firmware.Request
            $serial.Write("!GET,$queryId`n")
            $actual=Wait-FirmwareResponse $queryId 'GET' 1000
            Show-Result ('CONTROL actual firmware filter: {0}/{1}, generation={2}; Selected map must be reapplied before resuming.' -f $actual.Fields.scope,$actual.Fields.outcome,$actual.Fields.generation)
        } catch { Show-Result 'ERROR actual filter could not be queried; keep capture paused.' }
        return $false
    }
}
function Set-Qualification([string]$Value) {
    if (!$firmware.QualificationCapable) { throw 'This firmware does not support qualification control.' }
    $firmware.Request++; $id=$firmware.Request
    $serial.Write(('!QUALIFY,{0},{1}' -f $id,$Value.ToUpperInvariant())+"`n")
    $reply=Wait-FirmwareResponse $id 'QUALIFY'
    $expected=if ($Value -eq 'On') {'1'} else {'0'}
    if ($reply.Fields.enabled -ne $expected) { throw 'Firmware acknowledged a different qualification mode.' }
    $script:Qualification=$Value
    Show-Result "CONTROL qualification $Value; capture stays paused. S starts."
}
function Request-FirmwareStatistics {
    if (!$firmware.Capable) { return $false }
    $serial.Write('d')
    $firmware.Request++; $id=$firmware.Request
    $firmware.Responses.Remove("$id"); $firmware.PendingSnapshot=$null
    $serial.Write("!STATS,$id`n")
    try { $null=Wait-FirmwareResponse $id 'STATS_END' 15000; return $true }
    catch { Show-Result ('ERROR '+$_.Exception.Message); return $false }
}
function Request-TimingSnapshot {
    if (!$firmware.TimingCapable) {
        Show-Result '----- TIMING BEGIN -----'
        Show-Result 'ERROR this firmware does not advertise timing support; use the timing firmware project.'
        Show-Result '----- TIMING END -----'; return
    }
    if (!$firmware.ReportMarkersCapable) { Show-Result '----- TIMING REQUEST: older firmware response has no end marker -----' }
    $serial.Write('t')
}
function Stop-RecordingCapture([int]$TimeoutMilliseconds=3000) {
    if ($mode -ne 'Live' -or !$record -or !$serial -or !$serial.IsOpen) { return $true }
    try {
        # Shutdown has its own buffer: do not append drained traffic after a recording limit.
        $shutdownBuffer=New-InputBuffer
        $idleSeen=$false
        $null=$serial.ReadExisting() # Ignore an earlier pause acknowledgement already queued.
        $serial.Write("`ni")
        $wait=[Diagnostics.Stopwatch]::StartNew()
        while ($wait.ElapsedMilliseconds -lt $TimeoutMilliseconds) {
            Convert-InputChunk $shutdownBuffer $serial.ReadExisting() | ForEach-Object {
                if ($_ -match '^IDLE(?:$|,)') { $idleSeen=$true }
            }
            if ($idleSeen) { Show-Result 'CONTROL recording ended; RP2040 standby acknowledged. Nothing was cleared.'; return $true }
            Start-Sleep -Milliseconds 10
        }
        Show-Result 'ERROR recording ended, but standby acknowledgement timed out. RP2040 capture state is unconfirmed.'
    } catch {
        Show-Result ('ERROR could not confirm RP2040 standby: '+$_.Exception.Message)
    }
    return $false
}
function Pause-ForInvestigation([string]$Purpose='investigation') {
    $session.IdleSeen=$false
    $serial.Write("`ni") # End any interrupted line command before the legacy pause key.
    $wait=[Diagnostics.Stopwatch]::StartNew()
    $lastData=0L
    while ($wait.ElapsedMilliseconds -lt 3000) {
        $chunk=$serial.ReadExisting()
        if ($chunk) { $lastData=$wait.ElapsedMilliseconds }
        Convert-InputChunk $buffer $chunk | ForEach-Object { Process-Line $_ }
        if ($session.IdleSeen -and $wait.ElapsedMilliseconds-$lastData -ge 200) {
            if ($buffer.Partial -or $buffer.Discarding) {
                Process-Line 'INPUT_GAP partial line discarded at investigation boundary'
                $buffer.Partial=''; $buffer.Discarding=$false
            }
            Reset-InterpreterAssembly $state 'investigation boundary' | ForEach-Object { if ($null -eq $session.Selection) { Show-Result $_ } }
            return $true
        }
        Start-Sleep -Milliseconds 10
    }
    Show-Result "ERROR $Purpose menu not opened: pause acknowledgement/drain timed out. Check connection and firmware; capture state is uncertain."
    return $false
}
try {
    $openedOutputs=Open-RecordingOutputs -Plan $outputPlan -InputFile $InputFile
    $record=$openedOutputs.Writers['RecordPath']; $parsed=$openedOutputs.Writers['ParsedLog']
    if ($record) {
        Write-Host $(if ($mode -eq 'Live') {'Recording to '+$RecordPath+'. Press Q to end recording and close the console; P pauses capture.'} else {'Recording replay to '+$RecordPath+'. It ends when the input file is exhausted; Ctrl+C stops early.'})
        $recordingProgress=New-RecordingProgress $RecordPath $InputFormat $RecordTime $MaxRecordItems
        if ($mode -eq 'Replay') { $recordingProgress.StopHint='Press Ctrl+C to stop recording' }
        Update-RecordingProgress $recordingProgress $record -Force
    }
    if ($null -ne $session.Selection) { Show-InvestigationStatus }
    if ($mode -eq 'Replay') {
        Start-RecordingLimits $recordingProgress
        $replayReader=[IO.StreamReader]::new($InputFile)
        try {
            while (!$replayReader.EndOfStream) {
                if (Get-RecordingStopReason $recordingProgress) { break }
                Process-Line ($replayReader.ReadLine())
            }
        } finally { $replayReader.Dispose() }
    } else {
        $serial=[IO.Ports.SerialPort]::new($Port,$Baud,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
        $serial.Encoding=[Text.Encoding]::ASCII
        $serial.Handshake=[IO.Ports.Handshake]::None
        $serial.DtrEnable=$true; $serial.RtsEnable=$false
        $serial.ReadBufferSize=65536; $serial.WriteTimeout=1000
        $serial.Open()
        Write-Host "Opened $Port. Close PlatformIO Serial Monitor before using this console."
        if ($record) {
            Write-Host "RECORDING: S=resume, P=pause, T=bus speed, D=diagnostics, Q=end recording. View=$View; Outcome=$Outcome."
            Write-Host 'Recording starts automatically. Analysis/view-change keys are disabled during recording; select filters in the command parameters.'
        } else {
            Write-Host 'S=start, P=pause, T=bus speed, G=qualification On/Off, I=investigate, F=failure report, N/A=all, K=known, U=unknown, O=cycle Both/Success/Failure, D=diagnostics, Q=quit.'
        }
        Write-Host 'This program only controls passive capture; it never sends I2C queries or writes.'
        # Select only the USB presentation. Neither command clears or arms capture.
        $serial.Write('v')
        $clock=[Diagnostics.Stopwatch]::StartNew()
        $buffer=New-InputBuffer; $quit=$false
        # h is harmless on legacy firmware; never send line commands until CAPS confirms protocol 1.
        $serial.Write("`nh")
        $probe=[Diagnostics.Stopwatch]::StartNew()
        while (!$firmware.Capable -and $probe.ElapsedMilliseconds -lt 2000) {
            Convert-InputChunk $buffer $serial.ReadExisting() | ForEach-Object { Process-Line $_ }
            Start-Sleep -Milliseconds 10
        }
        if ($firmware.Capable) {
            if (Pause-ForInvestigation 'filter setup') {
                # Switch to readable first, so a previous RAW presentation cannot reject a new filter.
                $serial.Write('v')
                if (Set-SourceFilter) {
                    if ($InputFormat -eq 'Raw') { $serial.Write('r') }
                    if ($record) { Show-Result 'CONTROL filter setup acknowledged; automatic recording start follows. Nothing was cleared.' }
                    else { Show-Result 'CONTROL capture paused for acknowledged filter setup; S starts. Nothing was cleared.' }
                } else {
                    if ($record) { Show-Result 'CONTROL filter setup failed; recording stays paused. S retries setup; Q ends recording.' }
                    else { Show-Result 'CONTROL filter setup failed; capture remains paused. Select A/K/U or O to retry; S is blocked until acknowledged.' }
                }
            }
        } else {
            if ($InputFormat -eq 'Raw') { $serial.Write('r') }
            Show-Result 'CONTROL legacy firmware: PC filtering; firmware counters/details unavailable.'
        }
        if ($InputFormat -eq 'Raw') { Show-Result 'CONTROL RAW output remains unfiltered; any view/outcome selection is PC-side.' }
        Show-InvestigationStatus
        if ($record) {
            if ((!$firmware.Capable -or $firmware.Applied) -and !(Get-RecordingStopReason $recordingProgress)) {
                Start-RecordingLimits $recordingProgress
                $serial.Write('a'); Show-Result 'CONTROL recording enabled; capture start requested automatically. P pauses; Q ends recording.'
            } elseif (Get-RecordingStopReason $recordingProgress) { Show-Result 'CONTROL recording limit reached during setup; capture was not started.' }
            else { Show-Result 'ERROR recording file is open, but capture was NOT started because filter setup failed. S retries setup; Q ends recording.' }
        }
        while (!$quit -and !(Get-RecordingStopReason $recordingProgress) -and (!$MaxSeconds -or $clock.Elapsed.TotalSeconds -lt $MaxSeconds)) {
            $chunk=$serial.ReadExisting()
            Convert-InputChunk $buffer $chunk | ForEach-Object { Process-Line $_ }
            if (Get-RecordingStopReason $recordingProgress) { break }
            if (![Console]::IsInputRedirected -and [Console]::KeyAvailable) {
                $key=[Console]::ReadKey($true).Key
                if ($record -and $key -notin @('S','P','D','T','Q')) { continue }
                if ($key -in @('S','P','D','T','G','F','I','N','A','K','U','O','Q')) {
                    if ($recordingProgress) { $recordingProgress.Suspended=$true; Update-RecordingProgress $recordingProgress $record -Completed }
                    if ($key -notin @('D','T')) { Show-Result '-----' }
                }
                switch ($key) {
                    'S' {
                        if ($record -and $firmware.Capable -and !$firmware.Applied) {
                            if ((Pause-ForInvestigation 'recording filter retry') -and (Set-SourceFilter)) {
                                if ($InputFormat -eq 'Raw') { $serial.Write('r') }
                                Start-RecordingLimits $recordingProgress
                                $serial.Write('a'); Show-Result 'CONTROL recording filter acknowledged; start requested'
                            }
                        }
                        elseif ($firmware.Capable -and !$firmware.Applied) { Show-Result 'ERROR filter not acknowledged; select A/K/U or O to retry before starting.' }
                        else { $serial.Write('a'); Show-Result 'CONTROL start requested' }
                    }
                    'P' { $serial.Write('i'); Show-Result 'CONTROL pause requested' }
                    'D' {
                        if (!$firmware.ReportMarkersCapable) { Show-Result '----- DIAGNOSTICS REQUEST: older firmware response has no end marker -----' }
                        $serial.Write('d')
                    }
                    'T' { Request-TimingSnapshot }
                    'G' {
                        if (Pause-ForInvestigation 'qualification change') {
                            try {
                                $current=if ($Qualification) {$Qualification} elseif ($firmware.Qualification -and $firmware.Qualification.enabled -eq '0') {'Off'} else {'On'}
                                Set-Qualification $(if ($current -eq 'On') {'Off'} else {'On'})
                            } catch { Show-Result ('ERROR '+$_.Exception.Message) }
                        }
                    }
                    'F' {
                        if (Pause-ForInvestigation 'failure diagnostics') {
                            if (Request-FirmwareStatistics) { Format-FirmwareFailureReport $firmware | ForEach-Object { Show-Result $_ } }
                            Format-RegisterFailureReport $session.Failures | ForEach-Object { Show-Result $_ }
                            if ($firmware.Capable) { Show-Result 'PC report above covers delivered traffic only; use firmware report for pre-filter percentages.' }
                            Show-Result 'CONTROL capture stays paused; S resumes the current view, I investigates, N returns to normal view.'
                        }
                    }
                    'I' {
                        if (Pause-ForInvestigation) {
                            $selection=Select-InvestigationRegisters $session.Inventory
                            if ($null -ne $selection) {
                                $session.Selection=$selection
                                if (!$firmware.Capable -or $InputFormat -eq 'Raw' -or (Set-SourceFilter)) {
                                    Show-InvestigationStatus
                                    $serial.Write('a'); Show-Result 'CONTROL investigation capture start requested'
                                }
                            } else { Show-Result 'CONTROL investigation selection cancelled/unavailable; capture stays paused. S resumes.' }
                        }
                    }
                    {$_ -in @('N','A','K','U','O')} {
                        if (Pause-ForInvestigation 'view change') {
                            if ($key -eq 'O') { $Outcome=switch ($Outcome) {'Both' {'Success'} 'Success' {'Failure'} 'Failure' {'Both'}} }
                            else { $session.Selection=$null; $View=switch ($key) {'K' {'Known'} 'U' {'Unknown'} default {'All'}} }
                            if (!$firmware.Capable -or $InputFormat -eq 'Raw' -or (Set-SourceFilter)) {
                                Reset-InterpreterAssembly $state 'view changed' | ForEach-Object { Show-Result $_ }
                                Show-InvestigationStatus
                                Show-Result 'CONTROL capture stays paused; S resumes this view.'
                            }
                        }
                    }
                    'Q' { $quit=$true }
                }
                if ($key -in @('S','P','F','I','N','A','K','U','O','G','Q')) { Show-Result '-----' }
                if ($recordingProgress) { $recordingProgress.Suspended=$false }
            }
            Update-RecordingProgress $recordingProgress $record
            if (!$chunk) { Start-Sleep -Milliseconds 10 }
        }
        if ($buffer.Partial -or $buffer.Discarding) { Process-Line 'INPUT_GAP final partial line discarded' }
    }
} finally {
    try {
        if ($ShowFailures) {
            if ($mode -eq 'Live' -and $firmware.Capable -and $serial -and $serial.IsOpen -and (Pause-ForInvestigation 'final failure report')) {
                if (Request-FirmwareStatistics) { Format-FirmwareFailureReport $firmware | ForEach-Object { Show-Result $_ } }
            }
            if ($mode -eq 'Replay' -and $firmware.Snapshot) { Format-FirmwareFailureReport $firmware | ForEach-Object { Show-Result $_ } }
            Format-RegisterFailureReport $session.Failures | ForEach-Object { Show-Result $_ }
        }
        Reset-InterpreterAssembly $state 'session ended' | ForEach-Object { if ($null -eq $session.Selection) { Show-Result $_ } }
    } finally {
        try {
            if ($recordingProgress) { $recordingProgress.Suspended=$true }
            Update-RecordingProgress $recordingProgress $record -Completed
            $null=Stop-RecordingCapture
        } finally {
            if ($serial) { try { if ($serial.IsOpen) { $serial.Close() } } finally { $serial.Dispose() } }
            if ($record) { $record.Dispose() }; if ($parsed) { $parsed.Dispose() }
            if ($openedOutputs -and $openedOutputs.InputLock) { $openedOutputs.InputLock.Dispose() }
        }
    }
    Write-Host "Session: transactions=$($state.Transactions), decoded_properties=$($state.Parsed), untrusted_candidates=$($state.Candidates), unparsed=$($state.Unparsed), failed=$($state.Failures), incomplete=$($state.Incomplete), gaps=$($state.Gaps)"
    if ($recordingProgress -and $recordingProgress.StopReason) { Write-Host "Recording stopped: $($recordingProgress.StopReason). Recorded events=$($recordingProgress.Events)." }
    if ($ShowInventory) { Show-RegisterInventory $session.Inventory }
}

#requires -Version 5.1
$ErrorActionPreference='Stop'

function New-FailureDiagnostics([int]$Capacity=2048) {
    if ($Capacity -lt 1 -or $Capacity -gt 32768) { throw 'Diagnostic capacity must be 1..32768.' }
    [pscustomobject]@{Entries=@{};Capacity=$Capacity;Dropped=0L;Unattributed=0L;UnattributedExamples=[Collections.Generic.List[string]]::new();LossEvents=0L;LossCounters=@{};MalformedFrames=0L;Sequence=0L}
}

function Add-BoundedDiagnosticExample($Examples,[string]$Source) {
    if ($Source.Length -gt 1024) { $Source=$Source.Substring(0,1024)+' [example shortened]' }
    if (!$Examples.Contains($Source)) {
        if ($Examples.Count -ge 3) { $Examples.RemoveAt(0) }
        $Examples.Add($Source)
    }
}

function Add-UnattributedFailure($Diagnostics,[string]$Source) {
    $Diagnostics.Unattributed++
    Add-BoundedDiagnosticExample $Diagnostics.UnattributedExamples $Source
}

function Add-RegisterDiagnostic($Diagnostics,$Transaction,[bool]$Suspected=$false) {
    $Diagnostics.Sequence++
    $unknown= !$Transaction.Ok -and $Transaction.Source -match ' UNKNOWN(?: |$)'
    if ($null -eq $Transaction.Register) {
        if (!$Transaction.Ok) { Add-UnattributedFailure $Diagnostics $Transaction.Source }
        return
    }
    $key='{0}:{1}' -f $Transaction.Address,$Transaction.Register
    if (!$Diagnostics.Entries.ContainsKey($key)) {
        if ($Diagnostics.Entries.Count -ge $Diagnostics.Capacity) { $Diagnostics.Dropped++; return }
        $Diagnostics.Entries[$key]=[pscustomobject]@{
            Address=[int]$Transaction.Address;Register=[int]$Transaction.Register
            ReadOk=0L;ReadFail=0L;SuspectedReadFail=0L;ReadUnknown=0L;WriteOk=0L;WriteFail=0L;WriteUnknown=0L
            CurrentReadFail=0L;MaxReadFail=0L;Recent=[Collections.Generic.Queue[string]]::new()
            FirstFailure=$null;LastFailure=$null;LastReadSuccess=$null;LastReadResult='--'
            Reasons=@{};Examples=[Collections.Generic.List[string]]::new()
        }
    }
    $entry=$Diagnostics.Entries[$key]
    $now=[DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss.fff',[Globalization.CultureInfo]::InvariantCulture)
    if ($Transaction.Operation -eq 'Read') {
        if ($Transaction.Ok) {
            $entry.ReadOk++; $entry.CurrentReadFail=0; $entry.LastReadSuccess=$now; $entry.LastReadResult='OK'; $entry.Recent.Enqueue('.')
        } elseif ($unknown) {
            $entry.ReadUnknown++; $entry.CurrentReadFail=0; $entry.LastReadResult='UNKNOWN'; $entry.Recent.Enqueue('U')
        } else {
            if ($Suspected) { $entry.SuspectedReadFail++; $entry.LastReadResult='?FAIL'; $entry.Recent.Enqueue('?') }
            else { $entry.ReadFail++; $entry.LastReadResult='FAIL'; $entry.Recent.Enqueue('F') }
            $entry.CurrentReadFail++; $entry.MaxReadFail=[Math]::Max($entry.MaxReadFail,$entry.CurrentReadFail)
        }
        if ($entry.Recent.Count -gt 32) { $null=$entry.Recent.Dequeue() }
    } elseif ($Transaction.Operation -eq 'Write') {
        if ($Transaction.Ok) { $entry.WriteOk++ }
        elseif ($unknown) { $entry.WriteUnknown++ }
        else { $entry.WriteFail++ }
    }
    if (!$Transaction.Ok) {
        if ($null -eq $entry.FirstFailure) { $entry.FirstFailure=$now }
        $entry.LastFailure=$now
        $reasons=@([regex]::Matches($Transaction.Source,'address NACK|data NACK|missing final NACK|decode error|capture gap|capture stopped|OUTPUT_TRUNCATED') | ForEach-Object { $_.Value } | Select-Object -Unique)
        if (!$reasons.Count) { $reasons=@($(if ($unknown) {'unknown result'} else {'unspecified failure / RAW BAD or ACK validation'})) }
        foreach ($reason in $reasons) {
            if (!$entry.Reasons.ContainsKey($reason)) { $entry.Reasons[$reason]=0L }
            $entry.Reasons[$reason]++
        }
        Add-BoundedDiagnosticExample $entry.Examples $Transaction.Source
    }
}

function Update-FailureDiagnosticInput($Diagnostics,[string]$Line,[bool]$ObservedTransaction=$false) {
    # Exact two-segment firmware shape: an empty register-selector write plus
    # an unassociated read. This is evidence of an attempt, NOT a decoded link.
    if (!$ObservedTransaction -and $Line -match '^0x([0-9A-Fa-f]{2}) Reg 0x([0-9A-Fa-f]{2}) Write ; 0x([0-9A-Fa-f]{2}) Read((?: [0-9A-Fa-f]{2})*)(?: Dec (?:\d+|LE=\d+ BE=\d+)| Dec\[[\d ]+\])? FAIL(?: \([^\r\n]*\))*(?: \[OUTPUT_TRUNCATED\])?$') {
        $address=[Convert]::ToInt32($Matches[1],16)
        $reg=[Convert]::ToInt32($Matches[2],16)
        $readAddress=[Convert]::ToInt32($Matches[3],16)
        if ($address -eq $readAddress -and $address -le 127 -and $Line -notmatch 'OUTPUT_TRUNCATED') {
            Add-RegisterDiagnostic $Diagnostics ([pscustomobject]@{Address=$address;Register=$reg;Operation='Read';Ok=$false;Source=$Line}) $true
        } else { Add-UnattributedFailure $Diagnostics $Line }
    } elseif (!$ObservedTransaction -and $Line -match '^(?:0x.* FAIL(?: |$)|Undecodable.* FAIL(?: |$)|RAW,\d+,(?:P|Sr),BAD,)') {
        Add-UnattributedFailure $Diagnostics $Line
    }
    $loss=$Line -match '^(INPUT_GAP|OUTPUT_LOSS|ERROR|PARSER_ERROR)'
    if ($Line -match '^(DIAG,|USB,|CAPTURE,)') {
        foreach ($m in [regex]::Matches($Line,'(?:^|,)(bad|ring|fifo|queue|trace_drop|new_drop|rejected_lines|disconnect_drop|fault|bad_words)=(\d+)')) {
            $key=$m.Groups[1].Value; $value=[long]$m.Groups[2].Value
            if ($key -eq 'bad') { $Diagnostics.MalformedFrames=$value }
            elseif ($value -gt 0 -and (!$Diagnostics.LossCounters.ContainsKey($key) -or $value -gt $Diagnostics.LossCounters[$key])) { $loss=$true }
            $Diagnostics.LossCounters[$key]=$value
        }
    }
    if ($loss) {
        $Diagnostics.LossEvents++
        # Loss breaks claims about consecutive observed attempts. Keep lifetime
        # counts and maxima, but show the break explicitly in recent history.
        foreach ($entry in $Diagnostics.Entries.Values) {
            $entry.CurrentReadFail=0
            $entry.Recent.Enqueue('|')
            if ($entry.Recent.Count -gt 32) { $null=$entry.Recent.Dequeue() }
        }
    }
}

function Get-RegisterFailureSummary($Diagnostics) {
    foreach ($entry in @($Diagnostics.Entries.Values | Where-Object { $_.ReadFail+$_.SuspectedReadFail+$_.WriteFail+$_.ReadUnknown+$_.WriteUnknown -gt 0 } | Sort-Object Address,Register)) {
        $readAttempts=$entry.ReadOk+$entry.ReadFail+$entry.SuspectedReadFail
        $confirmed=$entry.ReadOk+$entry.ReadFail
        $rate=if ($readAttempts) { [Math]::Round(100.0*$entry.ReadOk/$readAttempts,2) } else { $null }
        $failRate=if ($readAttempts) { [Math]::Round(100.0*($entry.ReadFail+$entry.SuspectedReadFail)/$readAttempts,2) } else { $null }
        $confirmedRate=if ($confirmed) { [Math]::Round(100.0*$entry.ReadOk/$confirmed,2) } else { $null }
        [pscustomobject]@{Entry=$entry;ReadSuccessPercent=$rate;ReadFailurePercent=$failRate;ConfirmedReadSuccessPercent=$confirmedRate;Recent=($entry.Recent.ToArray() -join '')}
    }
}

function Format-RegisterFailureReport($Diagnostics) {
    'REGISTER FAILURE REPORT (this console session; received/delivered traffic only, including PC-hidden registers)'
    'R?FAIL = suspected read association from a failed compound chain; not a confirmed register read.'
    'Read success% = R_OK / (R_OK + R_FAIL + R?FAIL). UNKNOWN is excluded; no observed attempts = --.'
    'Read fail% = (R_FAIL + R?FAIL) / (R_OK + R_FAIL + R?FAIL). Successful reads count for each listed register is R_OK.'
    'Addr Reg   R_OK    R_FAIL  R?FAIL  R_UNK  Success%  Fail%     W_OK   W_FAIL W_UNK Now/Max Last read'
    $rows=@(Get-RegisterFailureSummary $Diagnostics)
    foreach ($row in $rows) {
        $e=$row.Entry
        $rate=if ($null -eq $row.ReadSuccessPercent) {'--'} else {$row.ReadSuccessPercent.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)+'%'}
        $failRate=if ($null -eq $row.ReadFailurePercent) {'--'} else {$row.ReadFailurePercent.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)+'%'}
        '0x{0:X2} 0x{1:X2} {2,-7} {3,-7} {4,-7} {5,-6} {6,-9} {7,-9} {8,-6} {9,-6} {10,-5} {11}/{12} {13}' -f $e.Address,$e.Register,$e.ReadOk,$e.ReadFail,$e.SuspectedReadFail,$e.ReadUnknown,$rate,$failRate,$e.WriteOk,$e.WriteFail,$e.WriteUnknown,$e.CurrentReadFail,$e.MaxReadFail,$e.LastReadResult
    }
    if (!$rows.Count) { 'No attributable register failures or unknown results observed yet.' }
    'Recent reads (oldest -> newest, last 32): .=OK F=FAIL ?=suspected FAIL U=UNKNOWN |=capture/output gap.'
    'Now/Max = consecutive failed observed read attempts per register, including suspected attempts; writes do not reset them.'
    foreach ($row in $rows) {
        $e=$row.Entry
        $confirmed=if ($null -eq $row.ConfirmedReadSuccessPercent) {'--'} else {$row.ConfirmedReadSuccessPercent.ToString('F2',[Globalization.CultureInfo]::InvariantCulture)+'%'}
        '0x{0:X2} 0x{1:X2} recent: {2}; confirmed-only read success: {3}' -f $e.Address,$e.Register,$row.Recent,$confirmed
        '  PC observation UTC: first problem={0}; last problem={1}; last successful read={2}' -f $e.FirstFailure,$e.LastFailure,$(if ($e.LastReadSuccess) {$e.LastReadSuccess} else {'none'})
        '  Reasons: '+ (($e.Reasons.Keys | Sort-Object | ForEach-Object { '{0}={1}' -f $_,$e.Reasons[$_] }) -join '; ')
        foreach ($example in $e.Examples) { '  Example: '+$example }
    }
    'Unattributed problem transactions/chains={0}; capture/output gap events={1}; diagnostic-capacity dropped={2}.' -f $Diagnostics.Unattributed,$Diagnostics.LossEvents,$Diagnostics.Dropped
    'Latest firmware malformed-frame counter={0}; malformed frames alone are not capture/output gaps.' -f $Diagnostics.MalformedFrames
    foreach ($example in $Diagnostics.UnattributedExamples) { '  Unattributed example: '+$example }
    'Rates describe received observations, not missing traffic. PC times are not bus timestamps; reasons may co-occur.'
    'A reported decode/capture failure is not proof of an electrical bus fault or a malfunctioning target device.'
    'No automatic diagnosis of a permanent fault: look for repeated failures without recovery versus isolated failures followed by OK reads.'
}

Export-ModuleMember -Function New-FailureDiagnostics,Add-RegisterDiagnostic,Update-FailureDiagnosticInput,Get-RegisterFailureSummary,Format-RegisterFailureReport

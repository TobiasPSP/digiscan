#requires -Version 5.1
$ErrorActionPreference='Stop'

function New-FirmwareSession {
    [pscustomobject]@{Capable=$false;TimingCapable=$false;QualificationCapable=$false;Qualification=$null;ReportMarkersCapable=$false;ReportKind=$null;Timing=$null;Replay=$false;Request=0L;Responses=@{};PendingSnapshot=$null;Snapshot=$null;Applied=$null;PendingFault=$null;FaultReasons=@{}}
}

function Read-ProtocolFields([string]$Line) {
    $parts=$Line.Split(','); $fields=@{}
    for ($i=1;$i -lt $parts.Count;$i++) {
        if ($parts[$i] -notmatch '^([A-Za-z_][A-Za-z0-9_]*)=([^,]*)$' -or $fields.ContainsKey($Matches[1])) { throw "Invalid protocol fields: $Line" }
        $fields[$Matches[1]]=$Matches[2]
    }
    return $fields
}

function Get-ProtocolNumber($Fields,[string]$Key,[long]$Maximum=4294967295) {
    if (!$Fields.ContainsKey($Key) -or $Fields[$Key] -notmatch '^\d+$') { throw "Missing/invalid protocol number: $Key" }
    $number=[long]::Parse($Fields[$Key],[Globalization.CultureInfo]::InvariantCulture)
    if ($number -gt $Maximum) { throw "Protocol number out of range: $Key" }
    return $number
}

function Update-FirmwareSession($Session,[string]$Line) {
    if ($Line -notmatch '^(CAPS|ACK|NAK|STATS_BEGIN|STATS_ROW|STATS_END|FAULT|FAULT_SEG|FAULT_EDGE|FAULT_TIME|TIMING|TIMING_RANGE|TIMING_FLAGS|QUAL|QUALIFIED|REPORT_BEGIN|REPORT_END),') { return $false }
    $kind=$Matches[1]
    try {
        $f=Read-ProtocolFields $Line
        switch ($kind) {
            'CAPS' { $Session.Capable=$f.protocol -eq '1' -and $f.filter -eq '1' -and $f.stats -eq '1'; $Session.TimingCapable=$f.timing -eq '1'; $Session.QualificationCapable=$f.qualification -eq '1'; $Session.ReportMarkersCapable=$f.report_markers -eq '1' }
            'QUAL' {
                $null=Get-ProtocolNumber $f 'enabled' 1
                foreach ($key in @('threshold_ns','threshold_cycles','scl_suppressed','sda_boundary_suppressed','overflow','corrected_frames')) { $null=Get-ProtocolNumber $f $key }
                $Session.Qualification=$f
            }
            'QUALIFIED' {
                $null=Get-ProtocolNumber $f 'txn'
                $null=Get-ProtocolNumber $f 'clock' 1
                $null=Get-ProtocolNumber $f 'boundary' 1
            }
            'REPORT_BEGIN' {
                if ($f.kind -notin @('D','T') -or $Session.ReportKind) { throw 'Invalid/nested report beginning.' }
                $Session.ReportKind=$f.kind
            }
            'REPORT_END' {
                if ($f.kind -notin @('D','T') -or $Session.ReportKind -ne $f.kind) { throw 'Unmatched report ending.' }
                $Session.ReportKind=$null
            }
            'TIMING' {
                if ($f.clock_kHz -notmatch '^(unknown|\d+\.\d+)$' -or $f.age_ms -notmatch '^(unknown|\d+)$') { throw 'Invalid timing estimate.' }
                foreach ($key in @('samples','active','pio_hz','resolution_ns','max_blind_ns')) { $null=Get-ProtocolNumber $f $key }
                $Session.Timing=$f
            }
            {$_ -in @('ACK','NAK')} {
                $id=Get-ProtocolNumber $f 'id'
                if ($id -eq $Session.Request) { $Session.Responses["$id"]=[pscustomobject]@{Kind=$kind;Fields=$f} }
            }
            'STATS_BEGIN' {
                $id=Get-ProtocolNumber $f 'id'
                if (!$Session.Replay -and $id -ne $Session.Request) { return $true }
                $expected=Get-ProtocolNumber $f 'rows' 512
                foreach ($key in @('epoch','total','failures','suppressed','unassociated','unassociated_fail','capacity_drop','chain_overflow')) { $null=Get-ProtocolNumber $f $key }
                $Session.PendingSnapshot=[pscustomobject]@{Id=$id;Expected=$expected;Header=$f;Rows=@{};Errors=$null}
            }
            'STATS_ROW' {
                $s=$Session.PendingSnapshot
                $id=Get-ProtocolNumber $f 'id'
                if (!$s -or $id -ne $s.Id) { return $true }
                if ($f.address -notmatch '^[0-7][0-9A-Fa-f]$' -or $f.reg -notmatch '^[0-9A-Fa-f]{2}$' -or $f.recent -notmatch '^[0-9A-Fa-f]{8}$') { throw 'Invalid counter identity/history.' }
                foreach ($key in @('decode_flags','ack_flags')) {
                    if ($f.ContainsKey($key) -and $f[$key] -notmatch '^[0-9A-Fa-f]{2}$') { throw 'Invalid diagnostic flags.' }
                }
                foreach ($key in @('r_ok','r_fail','w_ok','w_fail','now','max','reads')) { $null=Get-ProtocolNumber $f $key }
                $key="$($f.address):$($f.reg)"
                if ($s.Rows.ContainsKey($key) -or $s.Rows.Count -ge $s.Expected) { throw 'Duplicate/excess statistics row.' }
                $s.Rows[$key]=$f
            }
            'STATS_END' {
                $s=$Session.PendingSnapshot; $id=Get-ProtocolNumber $f 'id'
                if (!$s -or $id -ne $s.Id) { return $true }
                $count=Get-ProtocolNumber $f 'rows' 512
                if ($count -ne $s.Expected -or $s.Rows.Count -ne $count) { throw 'Incomplete statistics snapshot; previous snapshot retained.' }
                foreach ($key in @('partial_byte','empty_frame','ambiguous_setup','data_after_nack','frame_overflow')) { $null=Get-ProtocolNumber $f $key }
                $s.Errors=$f; $Session.Snapshot=$s; $Session.PendingSnapshot=$null
                if ($id -eq $Session.Request) { $Session.Responses["$id"]=[pscustomobject]@{Kind='STATS_END';Fields=$f} }
            }
            'FAULT_SEG' {
                foreach ($flag in ($f.flags -split '\|')) {
                    if ($flag -match '^(partial_byte|empty_frame|ambiguous_setup|data_after_nack|frame_overflow)$' -or $flag -match '^(addr_nack|data_nack|read_ack_fault|read_early_nack|missing_final_nack)=1$') {
                        if (!$Session.FaultReasons.ContainsKey($flag)) { $Session.FaultReasons[$flag]=0L }
                        $Session.FaultReasons[$flag]++
                    }
                }
            }
            'FAULT' {
                if ($f.association -notin @('explicit','unassociated')) { throw 'Invalid fault association.' }
                $Session.PendingFault=$f
            }
        }
    } catch {
        $Session.PendingSnapshot=$null
        Write-Warning $_.Exception.Message
    }
    return $true
}

function New-FilterMaps($Profile,[ValidateSet('All','Known','Unknown')][string]$View='All',$Selection=$null) {
    $maps=@{}
    if ($null -ne $Selection) {
        $maps[[int]$Selection.Address]=[byte[]]::new(32)
        foreach ($reg in $Selection.Registers) {
            $index=[int][Math]::Floor($reg/8)
            $maps[[int]$Selection.Address][$index]=$maps[[int]$Selection.Address][$index] -bor (1 -shl ($reg%8))
        }
    } elseif ($View -ne 'All') {
        if ($View -eq 'Unknown') { for ($address=8;$address -lt 120;$address++) { $maps[$address]=[byte[]](0..31 | ForEach-Object {255}) } }
        else { $maps[[int]$Profile.Address]=[byte[]]::new(32) }
        foreach ($property in $Profile.Properties) {
            foreach ($reg in $property.Registers) {
                $index=[int][Math]::Floor($reg/8); $mask=1 -shl ($reg%8)
                if ($View -eq 'Known') { $maps[[int]$Profile.Address][$index]=$maps[[int]$Profile.Address][$index] -bor $mask }
                else { $maps[[int]$Profile.Address][$index]=$maps[[int]$Profile.Address][$index] -band (255 -bxor $mask) }
            }
        }
    }
    return $maps
}

function Get-FilterCommands([long]$Id,[ValidateSet('All','Selected')][string]$Scope,[ValidateSet('Both','Success','Failure')][string]$Outcome,$Maps) {
    if ($Id -lt 1 -or $Id -gt 4294967295) { throw 'Filter request ID out of range.' }
    # ValidateSet accepts case-insensitive input; the wire protocol does not.
    $Scope=switch ($Scope) { 'All' {'All'} 'Selected' {'Selected'} }
    $Outcome=switch ($Outcome) { 'Both' {'Both'} 'Success' {'Success'} 'Failure' {'Failure'} }
    "!BEGIN,$Id,$Scope,$Outcome"
    if ($Scope -eq 'Selected') {
        foreach ($address in @($Maps.Keys | Sort-Object)) {
            if ($address -lt 0 -or $address -gt 127 -or $Maps[$address].Length -ne 32) { throw 'Invalid selection map.' }
            $hex=($Maps[$address] | ForEach-Object { '{0:X2}' -f $_ }) -join ''
            '!MAP,{0},{1:X2},{2}' -f $Id,$address,$hex
        }
    }
    "!COMMIT,$Id"
}

function Format-FirmwareFailureReport($Session) {
    $s=$Session.Snapshot
    if (!$s) { 'No complete firmware statistics snapshot available.'; return }
    'FIRMWARE FAILURE REPORT (since boot/clear; BEFORE USB filtering; counters saturate at 4294967295)'
    'Epoch={0}; transactions={1}; failures={2}; intentionally suppressed={3}; counter-capacity dropped={4}; chain-overflow={5}' -f $s.Header.epoch,$s.Header.total,$s.Header.failures,$s.Header.suppressed,$s.Header.capacity_drop,$s.Header.chain_overflow
    if ($s.Header.ContainsKey('output_rejected')) { 'USB-backpressure rejected transaction bundles='+$s.Header.output_rejected }
    'Addr Reg   R_OK    R_FAIL  Success% Fail%    W_OK    W_FAIL  Now/Max Recent reads (.=OK F=FAIL)'
    foreach ($r in @($s.Rows.Values | Where-Object { [long]$_.r_fail+[long]$_.w_fail -gt 0 } | Sort-Object address,reg)) {
        $total=[long]$r.r_ok+[long]$r.r_fail
        $ok=if ($total) { (100.0*[long]$r.r_ok/$total).ToString('F2',[Globalization.CultureInfo]::InvariantCulture)+'%' } else {'--'}
        $fail=if ($total) { (100.0*[long]$r.r_fail/$total).ToString('F2',[Globalization.CultureInfo]::InvariantCulture)+'%' } else {'--'}
        $bits=[Convert]::ToUInt32($r.recent,16); $history=''
        for ($i=[Math]::Min(32,[long]$r.reads)-1;$i -ge 0;$i--) { $history += $(if (($bits -shr $i) -band 1) {'F'} else {'.'}) }
        '0x{0} 0x{1} {2,-7} {3,-7} {4,-8} {5,-8} {6,-7} {7,-7} {8}/{9} {10}' -f $r.address,$r.reg,$r.r_ok,$r.r_fail,$ok,$fail,$r.w_ok,$r.w_fail,$r.now,$r.max,$history
        if ($r.decode_flags -and $r.ack_flags) {
            $names=@('partial_byte','empty_frame','ambiguous_setup','data_after_nack','frame_overflow')
            $flags=[Convert]::ToInt32($r.decode_flags,16); $ack=[Convert]::ToInt32($r.ack_flags,16)
            $seen=@(for ($bit=0;$bit -lt 5;$bit++) { if ($flags -band (1 -shl $bit)) { $names[$bit] } })
            if ($ack -band 1) { $seen+='address_NACK' }; if ($ack -band 2) { $seen+='data_NACK' }; if ($ack -band 4) { $seen+='read_ACK_fault' }
            if ($seen.Count) { '  Flags seen: '+($seen -join ', ') }
        }
    }
    'Unassociated transactions={0}, failures={1}; excluded from register percentages, available using All + Failure/Both.' -f $s.Header.unassociated,$s.Header.unassociated_fail
    'Decoder flags (transactions; may co-occur): '+ (($s.Errors.Keys | Where-Object {$_ -notin @('id','rows')} | Sort-Object | ForEach-Object {"$_=$($s.Errors[$_])"}) -join '; ')
    'Counters cover decoded traffic, not traffic lost before decoding. Streaks reset on capture gaps. Selection does not affect these counts.'
}

Export-ModuleMember -Function New-FirmwareSession,Update-FirmwareSession,New-FilterMaps,Get-FilterCommands,Format-FirmwareFailureReport

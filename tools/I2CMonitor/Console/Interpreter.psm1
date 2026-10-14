#requires -Version 5.1
$ErrorActionPreference = 'Stop'

function Convert-ProfileInteger($Value, [int]$Maximum = 65535) {
    $s = [string]$Value
    if ($s -match '^0x([0-9A-Fa-f]+)$') { $n = [Convert]::ToInt64($Matches[1],16) }
    elseif ($s -match '^\d+$') { $n = [long]$s }
    else { throw "Invalid unsigned integer: $s" }
    if ($n -lt 0 -or $n -gt $Maximum) { throw "Integer out of range: $s" }
    return [int]$n
}

function Import-DeviceProfile([string]$Path) {
    $manifest = Join-Path $PSScriptRoot '../vendor/powershell-yaml/0.4.12/powershell-yaml.psd1'
    if (!(Test-Path -LiteralPath $manifest)) { throw 'The bundled YAML dependency is missing. Copy the entire I2CMonitor module folder.' }
    Import-Module $manifest -Force
    $text = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    # Reject extra YAML documents instead of silently reading just the first.
    $documents = @(ConvertFrom-Yaml -Yaml $text -AllDocuments -Ordered)
    if ($documents.Count -ne 1) { throw 'Expected exactly one YAML document.' }
    $profile = $documents[0]
    if ($profile.schema_version -ne 1) { throw 'Unsupported schema_version.' }
    $address = Convert-ProfileInteger $profile.device.address 127
    if ($address -lt 8 -or $address -ge 0x78) { throw 'Reserved/10-bit addresses not supported.' }
    if ($profile.device.register_address_bytes -ne 1) { throw 'Only one-byte register selectors supported.' }
    $ids = @{}; $index = @{}; $items = [Collections.Generic.List[object]]::new()
    foreach ($p in $profile.properties) {
        $required = @('id','name','registers','operation','assembly','encoding','mask','shift','scale','offset','unit','precision','enum','valid_min','valid_max','evidence','note')
        foreach ($key in $required) { if (!$p.Contains($key)) { throw "Missing $key in property." } }
        # Tablet presentation metadata does not alter console numeric decoding.
        foreach ($key in $p.Keys) { if ($key -notin ($required + @('description','access','alternatives','bit_meanings','friendlydesc','atomic_step','atomic_step_basis','active_read'))) { throw "Unknown property key $key" } }
        if ([string]$p.id -cnotmatch '^[a-z][a-z0-9_]*$' -or $ids.ContainsKey([string]$p.id)) { throw 'Invalid/duplicate property id.' }
        $ids[[string]$p.id] = $true
        if ([string]::IsNullOrWhiteSpace($p.name) -or $p.name -match '[\r\n\x00-\x1F]') { throw 'Invalid property name.' }
        $regs = @($p.registers | ForEach-Object { Convert-ProfileInteger $_ 255 })
        if ($regs.Count -notin @(1,2) -or @($regs | Select-Object -Unique).Count -ne $regs.Count) { throw 'Properties require one or two distinct registers.' }
        if ($p.operation -notin @('read','read_write')) { throw 'Unsupported operation.' }
        if ($regs.Count -eq 1) {
            if ($p.assembly -ne 'single' -or $p.encoding -notin @('uint','ascii')) { throw 'Invalid single-register encoding/assembly.' }
        } elseif ($p.assembly -ne 'consecutive_target_reads' -or $p.operation -ne 'read' -or $p.encoding -notin @('uint_le','unsigned_code_le')) { throw 'Invalid pair encoding/assembly.' }
        $maximum = if ($regs.Count -eq 1) {255} else {65535}
        $mask = Convert-ProfileInteger $p.mask $maximum
        $shift = Convert-ProfileInteger $p.shift (8*$regs.Count-1)
        if (!$mask -or ($mask -band ((1 -shl $shift)-1))) { throw 'Mask/shift mismatch.' }
        $fieldMax = $mask -shr $shift
        if (($fieldMax -band ($fieldMax+1)) -ne 0) { throw 'Mask must describe a contiguous field.' }
        $minimum = Convert-ProfileInteger $p.valid_min $fieldMax
        $limit = Convert-ProfileInteger $p.valid_max $fieldMax
        if ($minimum -gt $limit) { throw 'Inverted valid range.' }
        $precision = Convert-ProfileInteger $p.precision 6
        $scale = [decimal]::Parse([string]$p.scale,[Globalization.CultureInfo]::InvariantCulture)
        $offset = [decimal]::Parse([string]$p.offset,[Globalization.CultureInfo]::InvariantCulture)
        if ($scale -le 0) { throw 'Scale must be positive.' }
        try {
            $null = ([decimal]$fieldMax * $scale + $offset)
        } catch { throw 'Conversion exceeds decimal range.' }
        if ($p.enum -isnot [Collections.IDictionary]) { throw 'enum must be a mapping.' }
        $enum = @{}
        foreach ($k in $p.enum.Keys) {
            $code = Convert-ProfileInteger $k $fieldMax
            if ($enum.ContainsKey($code)) { throw 'Duplicate enum code.' }
            $enum[$code] = [string]$p.enum[$k]
        }
        $bitMeanings = @{}
        if ($p.Contains('bit_meanings')) {
            if ($regs.Count -ne 1 -or $fieldMax -ne 1 -or $p.bit_meanings -isnot [Collections.IDictionary] -or $p.bit_meanings.Count -ne 2 -or $enum.Count) { throw 'bit_meanings requires a single-bit field, both codes, and no enum.' }
            foreach ($k in $p.bit_meanings.Keys) {
                $code = Convert-ProfileInteger $k 1
                $meaning = $p.bit_meanings[$k]
                if ($bitMeanings.ContainsKey($code) -or $meaning -isnot [Collections.IDictionary] -or $meaning.Count -ne 2 -or !$meaning.Contains('status') -or !$meaning.Contains('interpretation') -or $meaning.status -cnotin @('YES','NO') -or [string]::IsNullOrWhiteSpace($meaning.interpretation) -or $meaning.interpretation -match '[\x00-\x1F]') { throw 'Invalid bit_meanings entry.' }
                $bitMeanings[$code] = $meaning
            }
            if ($bitMeanings[0].status -eq $bitMeanings[1].status) { throw 'bit_meanings must explicitly distinguish YES and NO.' }
        }
        $item = [pscustomobject]@{Definition=$p;Registers=$regs;Mask=$mask;Shift=$shift;Minimum=$minimum;Maximum=$limit;Scale=$scale;Offset=$offset;Precision=$precision;Enum=$enum;BitMeanings=$bitMeanings}
        $items.Add($item)
        foreach ($reg in $regs) {
            if (!$index.ContainsKey($reg)) { $index[$reg] = [Collections.Generic.List[object]]::new() }
            $index[$reg].Add($item)
        }
    }
    # Multiple fields may share a byte, but overlapping definitions are rejected.
    foreach ($reg in $index.Keys) {
        $occupied = 0
        foreach ($item in $index[$reg]) {
            $mask = if ($item.Registers.Count -eq 2) {255} else {$item.Mask}
            if ($occupied -band $mask) { throw "Overlapping definitions for register $reg" }
            $occupied = $occupied -bor $mask
        }
    }
    return [pscustomobject]@{Address=$address;Index=$index;Properties=$items.ToArray();Source=$profile}
}

function New-InterpreterState($Profile,[switch]$ShowRemark) {
    return [pscustomobject]@{Profile=$Profile;ShowRemark=[bool]$ShowRemark;Pending=$null;RawPointer=$null;RawNumber=$null;LossCounters=@{};Transactions=0;Parsed=0;Candidates=0;Unparsed=0;Failures=0;Gaps=0;Incomplete=0;TransactionObserver=$null}
}

function New-InputBuffer {
    [pscustomobject]@{Partial='';Discarding=$false}
}

function Convert-InputChunk($Buffer,[string]$Chunk) {
    foreach ($part in ($Chunk -split '(?<=\n)')) {
        if (!$part) { continue }
        $complete=$part.EndsWith("`n")
        if (!$Buffer.Discarding) {
            if ($Buffer.Partial.Length+$part.Length -gt 4098) {
                'INPUT_GAP oversized serial line discarded'
                $Buffer.Partial=''; $Buffer.Discarding=$true
            } else {
                $Buffer.Partial+=$part
                if ($complete) {
                    $Buffer.Partial.TrimEnd("`r","`n")
                    $Buffer.Partial=''
                }
            }
        }
        if ($Buffer.Discarding -and $complete) { $Buffer.Discarding=$false }
    }
}

function Reset-InterpreterAssembly($State,[string]$Reason) {
    if ($null -ne $State.Pending) {
        $State.Incomplete++
        $p = $State.Pending.Property
        '0x{0:X2} R 0x{1:X2}+0x{2:X2} {3}: INCOMPLETE ({4})' -f $State.Profile.Address,$p.Registers[0],$p.Registers[1],$p.Definition.name,$Reason
    }
    $State.Pending = $null; $State.RawPointer = $null
}

function Format-PropertyValue($State,$Property,[int]$Raw,[string]$Operation,[string]$TransferResult='OK') {
    $p = $Property.Definition
    $code = ($Raw -band $Property.Mask) -shr $Property.Shift
    $status = 'OK'; $suffix = ''
    if ($code -lt $Property.Minimum -or $code -gt $Property.Maximum) { $status='OUT_OF_RANGE' }
    if ($Property.BitMeanings.Count) {
        $meaning = $Property.BitMeanings[$code]
        $value = '{0} (bit={1}) - {2}' -f $meaning.status,$code,$meaning.interpretation
    } elseif ($Property.Enum.Count) {
        if ($Property.Enum.ContainsKey($code)) { $value = $Property.Enum[$code] }
        else { $value = "code $code"; $status='UNDEFINED_CODE' }
    } elseif ($p.encoding -eq 'ascii') {
        $value = if ($code -ge 32 -and $code -le 126) { "'$([char]$code)'" } else { 'ASCII 0x{0:X2}' -f $code }
    } else {
        # Convert exactly once into the YAML property's explicit output unit.
        [decimal]$valueInDeclaredUnit = $code * $Property.Scale + $Property.Offset
        $value = $valueInDeclaredUnit.ToString("F$($Property.Precision)",[Globalization.CultureInfo]::InvariantCulture)
        if ($p.unit) { $value += " $($p.unit)" }
    }
    if ($p.alternatives -and $status -eq 'OK') { $status='CONFLICT' }
    if ($State.ShowRemark -and $p.note) { $suffix = " [$($p.note)]" }
    if ($Operation -eq 'Write') { $suffix = ' [observed write; application not verified]' + $suffix }
    $prefix='  '
    if ($Property.Registers.Count -eq 1 -and $Property.Mask -ne 255) {
        $width = 0; $bits = $Property.Mask -shr $Property.Shift
        while ($bits) { $width++; $bits = $bits -shr 1 }
        $range = if ($width -eq 1) { [string]$Property.Shift } else { '{0}:{1}' -f ($Property.Shift+$width-1),$Property.Shift }
        $prefix += "bit $range "
        if (!$Property.BitMeanings.Count) {
            $value = if ($value -eq "$code code") { "code $code" } else { "code $code - $value" }
            if ($State.ShowRemark -and !$Property.Enum.Count -and $p.description -and $p.unit -eq 'code') { $suffix = " [$($p.description)]" + $suffix }
        }
    }
    if ($TransferResult -eq 'FAIL') {
        $prefix='  FAIL: ' + $prefix.TrimStart()
        if ($status -eq 'OK') { $status='UNTRUSTED' }
        $suffix=' [failed transfer; decoded candidate only]' + $suffix
        $State.Candidates++
    } else { $State.Parsed++ }
    $statusText = if ($status -eq 'OK') { '' } else { " $status" }
    return ($prefix + ('{0}: {1}{2}{3}' -f $p.name,$value,$statusText,$suffix))
}

function Format-DecodedHeader($State,$Properties,[int]$Raw,[string]$Operation,[string]$TransferResult='OK') {
    $registers = ($Properties[0].Registers | ForEach-Object { '0x{0:X2}' -f $_ }) -join '+'
    $payload = if ($Operation -eq 'Write') { '{0:X2}' -f $Raw }
        elseif ($Properties[0].Registers.Count -eq 1 -and @($Properties | Where-Object { $_.Mask -ne 255 }).Count) { [Convert]::ToString($Raw,2).PadLeft(8,'0') }
        else { [string]$Raw }
    $direction = if ($Operation -eq 'Write') { 'W' } else { 'R' }
    $prefix = if ($TransferResult -eq 'FAIL') { 'FAIL: ' } else { '' }
    $prefix + ('0x{0:X2} {1} {2} {3}' -f $State.Profile.Address,$direction,$registers,$payload)
}

function Invoke-Transaction($State,$Transaction) {
    if ($null -ne $State.TransactionObserver) { & $State.TransactionObserver $Transaction | Out-Null }
    $State.Transactions++
    if (!$Transaction.Ok) {
        $State.Failures++
        $pending=$State.Pending
        Reset-InterpreterAssembly $State 'failed transaction'
        "FAIL: $($Transaction.Source)"
        # Retain useful decoding for observed bytes, but never cache a failed
        # component or present it as a successfully acquired measurement.
        if ($Transaction.Address -eq $State.Profile.Address -and $null -ne $Transaction.Register -and $Transaction.Bytes.Count -eq 1 -and $Transaction.Source -notmatch 'OUTPUT_TRUNCATED') {
            if ($null -ne $pending -and $Transaction.Operation -eq 'Read' -and $Transaction.Register -eq $pending.Property.Registers[1]) {
                Format-DecodedHeader $State @($pending.Property) ($pending.Low -bor ($Transaction.Bytes[0] -shl 8)) 'Read' 'FAIL'
                Format-PropertyValue $State $pending.Property ($pending.Low -bor ($Transaction.Bytes[0] -shl 8)) 'Read' 'FAIL'
            } elseif ($State.Profile.Index.ContainsKey([int]$Transaction.Register)) {
                $eligible = @($State.Profile.Index[[int]$Transaction.Register] | Where-Object { $_.Registers.Count -eq 1 -and ($Transaction.Operation -eq 'Read' -or ($Transaction.Operation -eq 'Write' -and $_.Definition.operation -eq 'read_write')) } | Sort-Object Shift -Descending)
                if ($eligible.Count) { Format-DecodedHeader $State $eligible ([int]$Transaction.Bytes[0]) $Transaction.Operation 'FAIL' }
                foreach ($property in $eligible) {
                    if ($property.Registers.Count -eq 1 -and ($Transaction.Operation -eq 'Read' -or ($Transaction.Operation -eq 'Write' -and $property.Definition.operation -eq 'read_write'))) {
                        Format-PropertyValue $State $property ([int]$Transaction.Bytes[0]) $Transaction.Operation 'FAIL'
                    }
                }
            }
        }
        return
    }
    if ($Transaction.Address -ne $State.Profile.Address) { $State.Unparsed++; $Transaction.Source; return }
    if ($null -ne $State.Pending) {
        $pending = $State.Pending
        if ($Transaction.Operation -eq 'Read' -and $Transaction.Register -eq $pending.Property.Registers[1] -and $Transaction.Bytes.Count -eq 1) {
            $State.Pending = $null
            Format-DecodedHeader $State @($pending.Property) ($pending.Low -bor ($Transaction.Bytes[0] -shl 8)) 'Read'
            Format-PropertyValue $State $pending.Property ($pending.Low -bor ($Transaction.Bytes[0] -shl 8)) 'Read'
            return
        }
        Reset-InterpreterAssembly $State 'next target transaction was not the matching high-byte read'
    }
    if ($null -eq $Transaction.Register -or !$State.Profile.Index.ContainsKey([int]$Transaction.Register) -or $Transaction.Bytes.Count -ne 1) {
        $State.Unparsed++; $Transaction.Source; return
    }
    $interpreted = $false
    $eligible = @($State.Profile.Index[[int]$Transaction.Register] | Where-Object { $Transaction.Operation -eq 'Read' -or ($Transaction.Operation -eq 'Write' -and $_.Definition.operation -eq 'read_write') } | Sort-Object Shift -Descending)
    if ($eligible.Count -and $eligible[0].Registers.Count -eq 1) { Format-DecodedHeader $State $eligible ([int]$Transaction.Bytes[0]) $Transaction.Operation }
    foreach ($property in $eligible) {
        if ($Transaction.Operation -eq 'Write' -and $property.Definition.operation -eq 'read') { continue }
        if ($Transaction.Operation -notin @('Read','Write')) { continue }
        if ($property.Registers.Count -eq 2) {
            if ($Transaction.Register -eq $property.Registers[0]) {
                $State.Pending = [pscustomobject]@{Property=$property;Low=[int]$Transaction.Bytes[0]}
            } else {
                $State.Incomplete++
                "$($Transaction.Source) [INCOMPLETE: high byte without preceding low byte]"
            }
        } else { Format-PropertyValue $State $property ([int]$Transaction.Bytes[0]) $Transaction.Operation }
        $interpreted = $true
    }
    if (!$interpreted) { $State.Unparsed++; "$($Transaction.Source) [not covered for this operation]" }
}

function New-Transaction($Address,$Register,$Operation,$Bytes,$Ok,$Source) {
    [pscustomobject]@{Address=[int]$Address;Register=$Register;Operation=$Operation;Bytes=@($Bytes);Ok=[bool]$Ok;Source=$Source}
}

function Convert-RawLine($State,[string]$Line) {
    if ($Line -notmatch '^RAW,(\d+),(P|Sr),(OK|BAD),(.*)$') {
        Reset-InterpreterAssembly $State 'malformed RAW frame'; $State.Gaps++; "PARSER_ERROR $Line"; return
    }
    [long]$number=0
    if (![long]::TryParse($Matches[1],[ref]$number)) {
        Reset-InterpreterAssembly $State 'invalid RAW sequence number'; $State.Gaps++; "PARSER_ERROR $Line"; return
    }
    $boundary=$Matches[2]; $ok=$Matches[3] -eq 'OK'; $data=$Matches[4]
    if ($null -ne $State.RawNumber -and $number -ne $State.RawNumber+1) {
        Reset-InterpreterAssembly $State 'RAW sequence gap'; $State.Gaps++; "INPUT_GAP RAW sequence $($State.RawNumber) -> $number"
    }
    $State.RawNumber=$number
    $tokens = if ($data) { @($data -split ' ') } else { @() }
    if (!$tokens.Count -or @($tokens | Where-Object { $_ -notmatch '^[0-9A-Fa-f]{2}[+-]$' }).Count -or $tokens.Count -gt 258) {
        Reset-InterpreterAssembly $State 'malformed RAW bytes'; $State.Gaps++; "PARSER_ERROR $Line"; return
    }
    $bytes = @($tokens | ForEach-Object { [Convert]::ToInt32($_.Substring(0,2),16) })
    $address=$bytes[0] -shr 1; $read=($bytes[0] -band 1) -ne 0
    $pointer=$State.RawPointer; $State.RawPointer=$null
    if ($tokens[0][2] -ne '+') { $ok=$false }
    for ($i=1;$i -lt $tokens.Count;$i++) {
        if ($read) {
            if (($i -lt $tokens.Count-1 -and $tokens[$i][2] -ne '+') -or ($i -eq $tokens.Count-1 -and $tokens[$i][2] -ne '-')) { $ok=$false }
        } elseif ($tokens[$i][2] -ne '+') { $ok=$false }
    }
    if ($ok -and !$read -and $bytes.Count -eq 2 -and $boundary -eq 'Sr') {
        $State.RawPointer=[pscustomobject]@{Address=$address;Register=$bytes[1]}; return
    }
    if ($boundary -ne 'P') {
        Reset-InterpreterAssembly $State 'unsupported RAW repeated-START chain'; $State.Unparsed++; $Line; return
    }
    $register=$null; $payload=@(); $operation='Probe'
    if ($read) {
        $operation='Read'; $payload=@($bytes | Select-Object -Skip 1)
        if ($null -ne $pointer -and $pointer.Address -eq $address) { $register=[int]$pointer.Register }
    } elseif ($bytes.Count -ge 2) { $operation='Write'; $register=[int]$bytes[1]; $payload=@($bytes | Select-Object -Skip 2) }
    $regText = if ($null -ne $register) { ' Reg 0x{0:X2}' -f $register } else { '' }
    $hex = ($payload | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    $source = ('0x{0:X2}{1} {2} {3} {4}' -f $address,$regText,$operation,$hex,$(if ($ok) {'OK'} else {'FAIL'})).Replace('  ',' ')
    Invoke-Transaction $State (New-Transaction $address $register $operation $payload $ok $source)
}

function Invoke-InputLine($State,[string]$Line,[ValidateSet('Readable','Raw')][string]$InputFormat='Readable') {
    $Line=$Line.TrimEnd("`r","`n")
    if (!$Line) { return }
    if ($Line.Length -gt 4096) { Reset-InterpreterAssembly $State 'oversized input line'; $State.Gaps++; 'PARSER_ERROR oversized input line'; return }
    if ($Line -match '^RAW,') {
        if ($InputFormat -eq 'Raw') { Convert-RawLine $State $Line }
        return
    }
    if ($Line -match '^(NEW,|EDGE,)') { return }
    if ($Line -match '^(CAPS,|ACK,|NAK,|STATS_BEGIN,|STATS_ROW,|STATS_END,|FAULT,|FAULT_SEG,|FAULT_EDGE,|FAULT_TIME,|TIMING,|TIMING_RANGE,|TIMING_FLAGS,|QUAL,|QUALIFIED,|REPORT_BEGIN,|REPORT_END,)') { $Line; return }
    if ($Line -match '^(DIAG,|USB,|CAPTURE,)') {
        $loss = $false
        foreach ($m in [regex]::Matches($Line,'(?:^|,)(bad|ring|fifo|queue|trace_drop|new_drop|rejected_lines|disconnect_drop|fault|bad_words)=(\d+)')) {
            $key=$m.Groups[1].Value; $value=[long]$m.Groups[2].Value
            if ($key -ne 'bad' -and $value -gt 0 -and (!$State.LossCounters.ContainsKey($key) -or $value -gt $State.LossCounters[$key])) { $loss=$true }
            $State.LossCounters[$key]=$value
        }
        if ($loss) { Reset-InterpreterAssembly $State 'reported capture/output loss'; $State.Gaps++ }
        $Line; return
    }
    if ($Line -match '^(OUTPUT_LOSS|ERROR|IDLE|CLEAR|ARMED|RP2040|USB:|INPUT_GAP)') {
        Reset-InterpreterAssembly $State 'capture/control boundary'
        if ($Line -match '^(INPUT_GAP|OUTPUT_LOSS|ERROR)') { $State.Gaps++ }
        $State.RawNumber=$null
        $Line; return
    }
    if ($Line -match '^(UI,|SYSTEM,|Decoder compile-time tests:)') { $Line; return }
    if ($Line -match '^Undecodable.* FAIL(?: |$)') {
        Reset-InterpreterAssembly $State 'undecodable transaction'
        $State.Transactions++; $State.Failures++; "FAIL: $Line"; return
    }
    if ($Line -notmatch '^0x') {
        Reset-InterpreterAssembly $State 'unrecognized input line'
        $State.Gaps++; "UNPARSED $Line"; return
    }
    if ($InputFormat -eq 'Raw') { return }
    if ($Line.Contains(';') -and $Line -match ' FAIL(?: \([^\r\n]*\))?(?: \[OUTPUT_TRUNCATED\])?$') {
        Reset-InterpreterAssembly $State 'failed compound transaction'
        $State.Transactions++; $State.Failures++
        "FAIL: $Line"
        return
    }
    # Consume the exact current firmware format; never interpret display text.
    if ($Line -notmatch '^0x([0-9A-Fa-f]{2})(?: Reg 0x([0-9A-Fa-f]{2}))? (Read|Write|Probe)((?: [0-9A-Fa-f]{2})*)(?: Dec (?:\d+|LE=\d+ BE=\d+)| Dec\[[\d ]+\])? (OK|FAIL|UNKNOWN)(?: \([^\r\n]*\))?(?: \[OUTPUT_TRUNCATED\])?$') {
        Reset-InterpreterAssembly $State 'unrecognized/compound transaction'; $State.Unparsed++; "UNPARSED $Line"; return
    }
    $address=[Convert]::ToInt32($Matches[1],16)
    $register=if ($Matches[2]) {[Convert]::ToInt32($Matches[2],16)} else {$null}
    $operation=$Matches[3]; $payload=@()
    if ($Matches[4].Trim()) { $payload=@($Matches[4].Trim().Split(' ') | ForEach-Object {[Convert]::ToInt32($_,16)}) }
    $ok=$Matches[5] -eq 'OK' -and $Line -notmatch 'OUTPUT_TRUNCATED|decode error|capture gap|capture stopped'
    Invoke-Transaction $State (New-Transaction $address $register $operation $payload $ok $Line)
}

Export-ModuleMember -Function Import-DeviceProfile,New-InterpreterState,Invoke-InputLine,Reset-InterpreterAssembly,New-InputBuffer,Convert-InputChunk

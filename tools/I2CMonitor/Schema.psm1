#requires -Version 5.1
# Private schema loader and property interpretation. Load the bundled exact YAML
# dependency, then validate types/references explicitly (no StrictMode reliance).
# Register assembly, bit extraction and scale/offset apply in that order; output
# units are taken from the definition, not inferred from property names.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReportSchema.ps1')

function Convert-ProfileInteger($Value, [int]$Maximum = 65535) {
    $s = [string]$Value
    if ($s -match '^0x([0-9A-Fa-f]+)$') { $n = [Convert]::ToInt64($Matches[1],16) }
    elseif ($s -match '^\d+$') { $n = [long]$s }
    else { throw "Invalid unsigned integer: $s" }
    if ($n -lt 0 -or $n -gt $Maximum) { throw "Integer out of range: $s" }
    return [int]$n
}

function Import-DeviceProfile([string]$Path) {
    $manifest = Join-Path $PSScriptRoot 'vendor/powershell-yaml/0.4.12/powershell-yaml.psd1'
    if (!(Test-Path -LiteralPath $manifest)) { throw 'Bundled YAML dependency is missing. Restore the complete I2CMonitor module directory.' }
    Import-Module $manifest -Force
    $text = Get-Content -LiteralPath $Path -Raw -Encoding utf8
    # Reject extra YAML documents instead of silently reading just the first.
    $documents = @(ConvertFrom-Yaml -Yaml $text -AllDocuments -Ordered)
    if ($documents.Count -ne 1) { throw 'Expected exactly one YAML document.' }
    $profile = $documents[0]
    $null=Get-ReportSchema $profile
    if ($profile.schema_version -ne 1) { throw 'Unsupported schema_version.' }
    $address = Convert-ProfileInteger $profile.device.address 127
    if ($address -lt 8 -or $address -ge 0x78) { throw 'Reserved/10-bit addresses not supported.' }
    if ($profile.device.register_address_bytes -ne 1) { throw 'Only one-byte register selectors supported.' }
    $ids = @{}; $index = @{}; $items = [Collections.Generic.List[object]]::new()
    foreach ($p in $profile.properties) {
        $required = @('id','name','registers','operation','assembly','encoding','mask','shift','scale','offset','unit','precision','enum','valid_min','valid_max','evidence','note')
        foreach ($key in $required) { if (!$p.Contains($key)) { throw "Missing $key in property." } }
        foreach ($key in $p.Keys) { if ($key -notin ($required + @('description','friendlydesc','access','alternatives','bit_meanings','atomic_step','atomic_step_basis'))) { throw "Unknown property key $key" } }
        if ([string]$p.id -cnotmatch '^[a-z][a-z0-9_]*$' -or $ids.ContainsKey([string]$p.id)) { throw 'Invalid/duplicate property id.' }
        $ids[[string]$p.id] = $true
        if ([string]::IsNullOrWhiteSpace($p.name) -or $p.name -match '[\r\n\x00-\x1F]') { throw 'Invalid property name.' }
        $regs = @($p.registers | ForEach-Object { Convert-ProfileInteger $_ 255 })
        if ($regs.Count -notin @(1,2,3) -or @($regs | Select-Object -Unique).Count -ne $regs.Count) { throw 'Properties require one to three distinct registers.' }
        for ($i=1;$i -lt $regs.Count;$i++) { if ($regs[$i] -ne $regs[0]+$i) { throw 'Multi-byte properties require consecutive register addresses.' } }
        if ($p.operation -notin @('read','read_write')) { throw 'Unsupported operation.' }
        if ($regs.Count -eq 1) {
            if ($p.assembly -ne 'single' -or $p.encoding -notin @('uint','ascii')) { throw 'Invalid single-register encoding/assembly.' }
        } elseif ($p.assembly -ne 'consecutive_target_reads' -or $p.operation -ne 'read' -or $p.encoding -notin @('uint_le','unsigned_code_le')) { throw 'Invalid pair encoding/assembly.' }
        $maximum = (1 -shl (8*$regs.Count))-1
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
            $mask = if ($item.Registers.Count -gt 1) {255} else {$item.Mask}
            if ($occupied -band $mask) { throw "Overlapping definitions for register $reg" }
            $occupied = $occupied -bor $mask
        }
    }
    return [pscustomobject]@{Address=$address;Index=$index;Properties=$items.ToArray();Source=$profile}
}

Export-ModuleMember -Function Import-DeviceProfile

#requires -Version 5.1
$ErrorActionPreference='Stop'

function New-RegisterInventory([int]$Capacity=2048) {
    if ($Capacity -lt 1 -or $Capacity -gt 32768) { throw 'Inventory capacity must be 1..32768.' }
    [pscustomobject]@{Entries=@{};Capacity=$Capacity;Dropped=0L;Registerless=0L;Sequence=0L}
}

function Test-DefinitionCoverage($Profile,$Transaction) {
    if ($Transaction.Address -ne $Profile.Address -or $null -eq $Transaction.Register -or $Transaction.Bytes.Count -ne 1 -or !$Profile.Index.ContainsKey([int]$Transaction.Register)) { return $false }
    foreach ($p in $Profile.Index[[int]$Transaction.Register]) {
        if ($Transaction.Operation -eq 'Read' -or ($Transaction.Operation -eq 'Write' -and $p.Definition.operation -eq 'read_write')) { return $true }
    }
    return $false
}

function Add-ObservedTransaction($Inventory,$Profile,$Transaction) {
    $Inventory.Sequence++
    if ($null -eq $Transaction.Register) { $Inventory.Registerless++; return }
    $key='{0}:{1}' -f $Transaction.Address,$Transaction.Register
    if (!$Inventory.Entries.ContainsKey($key)) {
        if ($Inventory.Entries.Count -ge $Inventory.Capacity) { $Inventory.Dropped++; return }
        $Inventory.Entries[$key]=[pscustomobject]@{
            Address=[int]$Transaction.Address;Register=[int]$Transaction.Register
            Reads=0L;Writes=0L;Failures=0L;Unknown=0L;UnknownReads=0L;UnknownWrites=0L
            MinLength=[int]::MaxValue;MaxLength=0;LastPayload='';LastOperation='';LastOk=$false
            Examples=[Collections.Generic.List[string]]::new();LastSequence=0L
        }
    }
    $entry=$Inventory.Entries[$key]
    if ($Transaction.Operation -eq 'Read') { $entry.Reads++ }
    if ($Transaction.Operation -eq 'Write') { $entry.Writes++ }
    if (!$Transaction.Ok) { $entry.Failures++ }
    if (!(Test-DefinitionCoverage $Profile $Transaction)) {
        $entry.Unknown++
        if ($Transaction.Operation -eq 'Read') { $entry.UnknownReads++ }
        if ($Transaction.Operation -eq 'Write') { $entry.UnknownWrites++ }
    }
    $entry.MinLength=[Math]::Min($entry.MinLength,$Transaction.Bytes.Count)
    $entry.MaxLength=[Math]::Max($entry.MaxLength,$Transaction.Bytes.Count)
    $entry.LastPayload=($Transaction.Bytes | Select-Object -First 256 | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
    $entry.LastOperation=$Transaction.Operation; $entry.LastOk=$Transaction.Ok; $entry.LastSequence=$Inventory.Sequence
    $example=$Transaction.Operation + ' ' + $entry.LastPayload + $(if ($Transaction.Ok) {' OK'} else {' FAIL'})
    if (!$entry.Examples.Contains($example)) {
        if ($entry.Examples.Count -ge 4) { $entry.Examples.RemoveAt(0) }
        $entry.Examples.Add($example)
    }
}

function Get-UnknownRegisterGroups($Inventory) {
    $group=$null
    foreach ($entry in @($Inventory.Entries.Values | Where-Object { $_.Unknown -gt 0 } | Sort-Object Address,Register)) {
        if ($null -eq $group -or $entry.Address -ne $group.Address -or $entry.Register -ne $group.End+1) {
            if ($null -ne $group) { $group }
            $group=[pscustomobject]@{Address=$entry.Address;Start=$entry.Register;End=$entry.Register;Registers=[Collections.Generic.List[int]]::new();Reads=0L;Writes=0L;Failures=0L;Unknown=0L}
        }
        $group.End=$entry.Register; $group.Registers.Add($entry.Register)
        $group.Reads+=$entry.Reads; $group.Writes+=$entry.Writes; $group.Failures+=$entry.Failures; $group.Unknown+=$entry.Unknown
    }
    if ($null -ne $group) { $group }
}

function Convert-RegisterSelection([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { throw 'Specify hexadecimal registers or ranges, e.g. 0x56-0x57,0x7A.' }
    $registers=[Collections.Generic.HashSet[int]]::new()
    foreach ($token in ($Text.Trim() -split '[,\s]+')) {
        if ($token -notmatch '^(?:0x)?([0-9a-fA-F]{1,2})(?:-(?:0x)?([0-9a-fA-F]{1,2}))?$') { throw "Invalid hexadecimal register/range: $token" }
        $first=[Convert]::ToInt32($Matches[1],16)
        $last=if ($Matches[2]) { [Convert]::ToInt32($Matches[2],16) } else { $first }
        if ($last -lt $first) { throw "Descending register range: $token" }
        for ($reg=$first;$reg -le $last;$reg++) { $null=$registers.Add($reg) }
    }
    @($registers | Sort-Object)
}

function New-InvestigationSelection([int]$Address,[int[]]$Registers) {
    if ($Address -lt 0 -or $Address -gt 127 -or !$Registers.Count -or @($Registers | Where-Object { $_ -lt 0 -or $_ -gt 255 }).Count) { throw 'Invalid investigation address/register set.' }
    [pscustomobject]@{Address=$Address;Registers=@($Registers | Sort-Object -Unique)}
}

function Test-InvestigationTransaction($Selection,$Transaction) {
    $null -ne $Selection -and $Transaction.Address -eq $Selection.Address -and $null -ne $Transaction.Register -and [int]$Transaction.Register -in $Selection.Registers
}

function Show-RegisterInventory($Inventory) {
    $groups=@(Get-UnknownRegisterGroups $Inventory)
    Write-Host 'Unknown observations (consecutive addresses are convenience groups, not inferred properties):'
    for ($i=0;$i -lt $groups.Count;$i++) {
        $g=$groups[$i]
        $range=if ($g.Start -eq $g.End) { '0x{0:X2}' -f $g.Start } else { '0x{0:X2}-0x{1:X2}' -f $g.Start,$g.End }
        Write-Host ('{0,3}  0x{1:X2}  {2,-11} R:{3,-6} W:{4,-6} FAIL:{5,-5} uncovered:{6}' -f ($i+1),$g.Address,$range,$g.Reads,$g.Writes,$g.Failures,$g.Unknown)
    }
    if (!$groups.Count) { Write-Host 'No unknown register observations yet. Resume capture with S and try again.' }
    Write-Host "Inventory: $($Inventory.Entries.Count) registers; registerless=$($Inventory.Registerless); capacity-dropped=$($Inventory.Dropped)."
}

function Select-InvestigationRegisters($Inventory,[scriptblock]$Prompt={param($Text) Read-Host $Text}) {
    Show-RegisterInventory $Inventory
    $groups=@(Get-UnknownRegisterGroups $Inventory)
    if (!$groups.Count) { return $null }
    while ($true) {
        $answer= & $Prompt 'Select group number (Q cancels; capture stays paused)'
        if ($answer -match '^(?i)q$') { return $null }
        [int]$number=0
        if (![int]::TryParse($answer,[ref]$number) -or $number -lt 1 -or $number -gt $groups.Count) { Write-Host 'Choose a listed group number.'; continue }
        $group=$groups[$number-1]
        $default=if ($group.Start -eq $group.End) { '0x{0:X2}' -f $group.Start } else { '0x{0:X2}-0x{1:X2}' -f $group.Start,$group.End }
        foreach ($entry in @($Inventory.Entries.Values | Where-Object { $_.Address -eq $group.Address } | Sort-Object Register)) {
            $last=if ($entry.LastPayload.Length -gt 47) { $entry.LastPayload.Substring(0,47)+' ...' } else { $entry.LastPayload }
            Write-Host ('  0x{0:X2} R:{1} W:{2} uncovered R/W:{3}/{4} lengths:{5}..{6} last:{7} {8} {9}' -f $entry.Register,$entry.Reads,$entry.Writes,$entry.UnknownReads,$entry.UnknownWrites,$entry.MinLength,$entry.MaxLength,$entry.LastOperation,$last,$(if ($entry.LastOk) {'OK'} else {'FAIL'}))
        }
        while ($true) {
            $answer= & $Prompt "Enter register(s) in hex, separated by commas; ranges allowed (e.g. 0x6E-0x6F). This replaces the selection. Enter keeps $default; Q cancels"
            if ($answer -match '^(?i)q$') { return $null }
            if ([string]::IsNullOrWhiteSpace($answer)) { $answer=$default }
            try { $registers=@(Convert-RegisterSelection $answer) } catch { Write-Host $_.Exception.Message; continue }
            foreach ($reg in $registers) {
                if (!$Inventory.Entries.ContainsKey(('{0}:{1}' -f $group.Address,$reg))) { Write-Host ('Note: 0x{0:X2} has not been observed yet.' -f $reg) }
            }
            return (New-InvestigationSelection $group.Address $registers)
        }
    }
}

Export-ModuleMember -Function New-RegisterInventory,Add-ObservedTransaction,Get-UnknownRegisterGroups,Convert-RegisterSelection,New-InvestigationSelection,Test-InvestigationTransaction,Show-RegisterInventory,Select-InvestigationRegisters

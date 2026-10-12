<#
.SYNOPSIS
Generate compiled fallback profiles, reports and experiment headers.
.DESCRIPTION
Validates all YAML files in a directory and generates compiled fallback property,
report and experiment tables. External profiles still resolve at runtime; this
command is only needed when maintaining the built-in defaults.
.EXAMPLE
New-ProfileHeaders -ProfileDirectory .\profiles -Output .\include\generated_profiles.h
.PARAMETER ProfileDirectory
Directory containing chip YAML files. Defaults to profiles in the firmware tree.
.PARAMETER Output
Property header output path. Report/experiment headers go to the same directory.
.NOTES
Supply explicit paths when using the module outside the firmware checkout.
#>
function New-ProfileHeaders {
    [CmdletBinding(PositionalBinding = $false)]
    param([string]$ProfileDirectory, [string]$Output)
    $ErrorActionPreference = 'Stop'
    . (Join-Path $PSScriptRoot 'ReportSchema.ps1')
    $project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if (!$ProfileDirectory) { $ProfileDirectory = Join-Path $project 'profiles' }
    if (!$Output) { $Output = Join-Path $project 'include/generated_profiles.h' }
    Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force
    function Convert-ProfileInteger($Value, [int]$Maximum = 65535) {
        $s=[string]$Value
        if ($s -match '^0x([0-9A-Fa-f]+)$') { $n=[Convert]::ToInt64($Matches[1],16) }
        elseif ($s -match '^\d+$') { $n=[long]$s } else { throw "Invalid integer: $s" }
        if ($n -lt 0 -or $n -gt $Maximum) { throw "Integer out of range: $s" }
        [int]$n
    }
    function Quote-C([string]$Text) {
        $Text = Display-Text $Text
        $escaped = $Text.Replace('\','\\').Replace('"','\"').Replace("`r",'\r').Replace("`n",'\n')
        return '"' + $escaped + '"'
    }
    function Number-C($Number) { ([double]$Number).ToString('R',[Globalization.CultureInfo]::InvariantCulture) }
    function Bool-C($Value) { if ($Value) { 'true' } else { 'false' } }
    function Display-Text([string]$Text) {
        # Some legacy descriptions already contain UTF-8 decoded as Windows-1252.
        # Repair only when the candidate is valid UTF-8 and round-trips exactly.
        if ($Text.Contains([string][char]0x00E2) -or $Text.Contains([string][char]0x00C3)) {
            try {
                $windows = [Text.Encoding]::GetEncoding(1252)
                $utf8 = New-Object Text.UTF8Encoding($false, $true)
                $candidate = $utf8.GetString($windows.GetBytes($Text))
                if ($windows.GetString($utf8.GetBytes($candidate)) -ceq $Text) { $Text = $candidate }
            } catch { }
        }
        $Text = $Text.Replace([string][char]0x2013, '-').Replace([string][char]0x2014, '-')
        $Text = $Text.Replace([string][char]0x2018, "'").Replace([string][char]0x2019, "'")
        $Text = $Text.Replace([string][char]0x201C, '"').Replace([string][char]0x201D, '"')
        $Text = $Text.Replace([string][char]0x2212, '-').Replace([string][char]0x00D7, 'x')
        $Text = $Text.Replace([string][char]0x2264, '<=').Replace([string][char]0x2265, '>=')
        return $Text.Replace('**', '').Replace('`', '').Trim()
    }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('// Generated from schema-validated YAML by New-ProfileHeaders in I2CMonitor. Not a datasheet verification. Do not edit.')
    $lines.Add('#pragma once')
    $lines.Add('#include "profile_types.h"')
    $lines.Add('namespace builtin_decoding {')
    $lines.Add('using decoding::EnumValue; using decoding::Property; using decoding::Profile;')
    $entries = [Collections.Generic.List[string]]::new()
    $profiles = @(Get-ChildItem -LiteralPath $ProfileDirectory -Filter '*.yaml' | Sort-Object Name)
    if (!$profiles.Count) { throw 'No YAML profiles found.' }
    $profileIds = @{}; $index = 0; $profileIndex = 0
    $profileEntries = [Collections.Generic.List[string]]::new()
    $reportLines = [Collections.Generic.List[string]]::new()
    $reportEntries = [Collections.Generic.List[string]]::new()
    $reportId=0
    foreach ($file in $profiles) {
        $profile = Import-DeviceProfile $file.FullName
        $device = $profile.Source.device
        if (!$device) { throw 'Validated device metadata is missing.' }
        $id = [string]$device.id
        if ($id -cnotmatch '^[a-z][a-z0-9_.-]{0,47}$' -or $profileIds.ContainsKey($id) -or [string]::IsNullOrWhiteSpace($device.name) -or [string]$device.name -match '[\r\n]' -or ([string]$device.name).Length -gt 80) { throw 'Invalid/duplicate device id or display name.' }
        $profileIds[$id] = $true
        foreach ($page in @(Get-ReportSchema $profile.Source)) {
            $groupEntries=[Collections.Generic.List[string]]::new();$groupId=0
            foreach ($group in $page.groups) {
                $items=@($group.items | ForEach-Object {Quote-C $_})
                $reportLines.Add('static const char *const items_'+$reportId+'_'+$groupId+'[] = {'+($items -join ',')+'};')
                $groupEntries.Add('{'+(Quote-C $group.header)+',items_'+$reportId+'_'+$groupId+','+$items.Count+'},')
                $groupId++
            }
            $reportLines.Add('static const reporting::Group groups_'+$reportId+'[] = {'+($groupEntries -join '')+'};')
            $reportEntries.Add('{'+(Quote-C $id)+','+(Quote-C $page.header)+',groups_'+$reportId+','+$groupId+'},')
            $reportId++
        }
        $revision = if ($device.Contains('profile_revision')) { Convert-ProfileInteger $device.profile_revision } else { 1 }
        if ($revision -lt 1) { throw 'Profile revision must be 1..65535.' }
        $typical = if ($device.Contains('typical_addresses')) { @($device.typical_addresses | ForEach-Object { Convert-ProfileInteger $_ 127 }) } else { @($profile.Address) }
        if (!$typical.Count -or $typical.Count -gt 112 -or @($typical | Select-Object -Unique).Count -ne $typical.Count -or @($typical | Where-Object { $_ -lt 8 -or $_ -gt 119 }).Count) { throw 'Invalid typical_addresses.' }
        $lines.Add('static const uint8_t typical_' + $profileIndex + '[] = {' + ($typical -join ',') + '};')
        $first = $index
        $readProtocol = 0
        $acquisition = $profile.Source.transport.active_read
        if ($acquisition) {
            if ($acquisition.protocol -ne 'single_byte_pointer_restart' -or $acquisition.clock_Hz -ne 10000 -or $acquisition.max_registers -ne 3) {
                throw "Unsupported active-read protocol in $id"
            }
            $readProtocol = 1
        }
        foreach ($item in $profile.Properties) {
            $p = $item.Definition
            $enum = @($item.Enum.Keys | Sort-Object)
            $enumPointer = 'nullptr'
            if ($enum.Count) {
                $enumPointer = 'enum_' + $index
                $lines.Add('static const EnumValue ' + $enumPointer + '[] = {')
                foreach ($code in $enum) { $lines.Add(('{{{0}, {1}}},' -f $code,(Quote-C $item.Enum[$code]))) }
                $lines.Add('};')
            }
            $bit0 = 'nullptr'; $bit1 = 'nullptr'; $meaning0 = 'nullptr'; $meaning1 = 'nullptr'
            if ($item.BitMeanings.Count) {
                $bit0 = Quote-C $item.BitMeanings[0].status
                $bit1 = Quote-C $item.BitMeanings[1].status
                $meaning0 = Quote-C $item.BitMeanings[0].interpretation
                $meaning1 = Quote-C $item.BitMeanings[1].interpretation
            }
            $high = if ($item.Registers.Count -gt 1) { $item.Registers[-1] } else { 0 }
            $atomicStep = 0.0
            if ($p.Contains('atomic_step')) {
                if ($p.atomic_step -isnot [ValueType] -or $p.atomic_step -is [bool]) { throw "atomic_step must be numeric: $($p.id)" }
                $atomicStep = [double]$p.atomic_step
                if ([double]::IsNaN($atomicStep) -or [double]::IsInfinity($atomicStep) -or $atomicStep -lt 0 -or $atomicStep -gt ([double]::MaxValue / 100)) { throw "Invalid atomic_step: $($p.id)" }
                if ($atomicStep -gt 0 -and ($item.BitMeanings.Count -or $enum.Count -or $p.encoding -eq 'ascii' -or $p.alternatives)) { throw "atomic_step requires an unambiguous numeric property: $($p.id)" }
            }
            if ($p.Contains('friendlydesc') -and ($p.friendlydesc -isnot [string] -or $p.friendlydesc.Length -gt 2048)) { throw "friendlydesc must be a string of at most 2048 characters: $($p.id)" }
            if ($p.Contains('access') -and $p.access -isnot [string]) { throw "access must be text: $($p.id)" }
            $access = if ($p.access) { Display-Text $p.access } elseif ($p.operation -eq 'read_write') { 'R/W' } else { 'R' }
            $access = $access.Replace(' (inferred)', ', inferred')
            # Acquisition capability depends on the read protocol/layout, not write access
            # or interpretation uncertainty. operation describes passive parsing directions.
            $activeReadable = $readProtocol -eq 1 -and $item.Registers.Count -le 3
            $fields = @((Quote-C $p.id),(Quote-C $p.name),(Quote-C $p.unit),(Quote-C $p.note),
                $profile.Address,$item.Registers[0],$high,$item.Registers.Count,$item.Shift,$item.Precision,
                $item.Mask,$item.Minimum,$item.Maximum,(Number-C $item.Scale),(Number-C $item.Offset),
                (Bool-C ($p.operation -eq 'read_write')),(Bool-C ($p.encoding -eq 'ascii')),
                (Bool-C ([bool]$p.alternatives)),(Bool-C ($p.encoding -eq 'unsigned_code_le')),
                $bit0,$bit1,$enumPointer,$enum.Count,$meaning0,$meaning1,(Quote-C (Display-Text $p.description)),(Number-C $atomicStep),(Quote-C $p.friendlydesc),(Quote-C $access),$profileIndex,(Bool-C $activeReadable))
            $entries.Add('{' + ($fields -join ', ') + '},')
            $index++
        }
        if ($index -eq $first -or $index-$first -gt 512) { throw 'Profile must contain 1..512 properties.' }
        $profileEntries.Add('{' + (@((Quote-C $id),(Quote-C $device.name),$revision,('typical_'+$profileIndex),$typical.Count,$first,($index-$first),(Quote-C $profile.Source.provenance.reference_url),$readProtocol,'nullptr') -join ',') + '},')
        $profileIndex++
    }
    $lines.Add('static const Property definitions[] = {')
    foreach ($entry in $entries) { $lines.Add($entry) }
    $lines.Add('};')
    $lines.Add('constexpr unsigned DefinitionCount = sizeof(definitions)/sizeof(definitions[0]);')
    $lines.Add('static const Profile profiles[] = {')
    foreach ($entry in $profileEntries) { $lines.Add($entry) }
    $lines.Add('};')
    $lines.Add('constexpr unsigned ProfileCount = sizeof(profiles)/sizeof(profiles[0]);')
    $lines.Add('constexpr unsigned PropertyCount = 512; // Bounded active property instances, across all addresses.')
    $lines.Add('}')
    $lines.Add('#include "runtime_profiles.h"')
    [IO.File]::WriteAllLines($Output,$lines,(New-Object Text.UTF8Encoding($false)))
    Write-Host "Generated $index validated properties from $($profiles.Count) profile(s): $Output"
    New-ExperimentHeader -ProfileDirectory $ProfileDirectory -Output (Join-Path (Split-Path $Output -Parent) 'generated_experiments.h')
    $reportOutput=Join-Path (Split-Path $Output -Parent) 'generated_reports.h'
    $reportHeader=@('// Generated unresolved report descriptors. Resolution and All expansion happen on the tablet.', '#pragma once','#include "report_model.h"','namespace builtin_reporting {','using reporting::Page;')+@($reportLines)+@('static const Page pages[] = {')+@($reportEntries)+@('};','constexpr unsigned PageCount=sizeof(pages)/sizeof(pages[0]);','}','#include "runtime_reports.h"')
    [IO.File]::WriteAllLines($reportOutput,$reportHeader,(New-Object Text.UTF8Encoding($false)))
    
}

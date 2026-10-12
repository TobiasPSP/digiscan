<#
.SYNOPSIS
Generate compiled identification, graph and register filter descriptors.
.DESCRIPTION
Validates identification evidence, graph references and recorder filter lists
before generating the fallback C++ descriptors. Does not contact hardware.
.EXAMPLE
New-ExperimentHeader -ProfileDirectory .\profiles -Output .\include\generated_experiments.h
.PARAMETER ProfileDirectory
Directory of chip YAML files; defaults to the firmware profiles directory.
.PARAMETER Output
Generated C++ header; defaults to include/generated_experiments.h in the checkout.
.NOTES
Supply explicit paths when using the module outside the firmware checkout.
#>
function New-ExperimentHeader {
    [CmdletBinding(PositionalBinding=$false)]
    param([string]$ProfileDirectory,[string]$Output)
    $ErrorActionPreference='Stop'
    $project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
    if(!$ProfileDirectory){$ProfileDirectory=Join-Path $project 'profiles'}
    if(!$Output){$Output=Join-Path $project 'include/generated_experiments.h'}
    Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force
    function Convert-ProfileInteger($Value,[int]$Maximum=65535){
        $s=[string]$Value
        if($s -match '^0x([0-9A-Fa-f]+)$'){$n=[Convert]::ToInt64($Matches[1],16)}
        elseif($s -match '^\d+$'){$n=[long]$s}else{throw "Invalid integer: $s"}
        if($n -lt 0 -or $n -gt $Maximum){throw "Integer out of range: $s"}
        [int]$n
    }
    function Quote([string]$s){
        if(!$s -or $s.Length -gt 192 -or $s -match '[\x00-\x1f]'){throw 'Invalid experiment metadata text'}
        '"'+$s.Replace('\','\\').Replace('"','\"')+'"'
    }
    $lines=[Collections.Generic.List[string]]::new()
    $lines.Add('#pragma once');$lines.Add('#include "experiment.h"');$lines.Add('namespace builtin_experiment {')
    $lines.Add('using experiment::Rule; using experiment::GraphLine; using experiment::Graph; using experiment::FilterList;')
    $rules=[Collections.Generic.List[string]]::new();$graphs=[Collections.Generic.List[string]]::new();$index=0
    $filters=[Collections.Generic.List[string]]::new();$filterIndex=0
    foreach($file in Get-ChildItem $ProfileDirectory -Filter '*.yaml' | Sort-Object Name){
        $profile=Import-DeviceProfile $file.FullName;$src=$profile.Source;$id=Quote $src.device.id
        $names=@{}
        foreach($f in @($src.filter_lists)){
            if(!$f){continue}
            $name=Quote $f.name
            if($names.ContainsKey([string]$f.name)){throw 'Duplicate filter-list name'}
            $names[[string]$f.name]=$true
            $registers=@($f.registers | ForEach-Object { Convert-ProfileInteger $_ 255 })
            if(!$registers.Count -or $registers.Count -gt 128 -or @($registers | Sort-Object -Unique).Count -ne $registers.Count){throw 'Filter list requires 1..128 unique registers'}
            $lines.Add("static const uint8_t filter_$filterIndex`[]={"+($registers -join ',')+'};')
            $filters.Add("{$id,$name,filter_$filterIndex,$($registers.Count)},");$filterIndex++
        }
        foreach($r in @($src.identification)){
            if(!$r){continue};$reg=Convert-ProfileInteger $r.register 255;$mask=Convert-ProfileInteger $r.mask 255
            $min=Convert-ProfileInteger $r.min 255;$max=Convert-ProfileInteger $r.max 255
            $kind=([string]$r.class).ToUpperInvariant()
            if(!$mask -or $min -gt $max -or !$r.label -or $kind -notin @('A','B','C')){throw 'Invalid identification rule; class A/B/C required'}
            if($kind -eq 'A' -and ($r.exclusive_verified -isnot [bool] -or !$r.exclusive_verified)){throw 'Class A requires exclusive_verified: true'}
            if($kind -eq 'B' -and ($r.mandatory_verified -isnot [bool] -or !$r.mandatory_verified)){throw 'Class B requires mandatory_verified: true'}
            if(!$r.source){throw 'Identification rule requires a source reference'}
            $rules.Add("{$id,$(Quote $r.label),$reg,$mask,$min,$max,identification::Class::$kind},")
        }
        foreach($g in @($src.graphs)){
            if(!$g){continue};$entries=[Collections.Generic.List[string]]::new()
            if(!$g.header -or @($g.lines).Count -lt 1 -or @($g.lines).Count -gt 8){throw 'Graph requires title and 1..8 lines'}
            foreach($l in $g.lines){
                if($l.property -cnotmatch '^0x[0-9A-Fa-f]{2}_([01]{8}|[01]{16}|[01]{24})$'){throw 'Invalid graph reference'}
                $min=[double]$l.min;$max=[double]$l.max
                if([double]::IsNaN($min) -or [double]::IsInfinity($min) -or [double]::IsNaN($max) -or [double]::IsInfinity($max) -or $min -ge $max){throw 'Invalid graph range'}
                if($null -ne $l.area -and $l.area -isnot [bool]){throw 'Graph area must be a YAML boolean'}
                $color=Convert-ProfileInteger $l.color 16777215;$area=if($l.area){'true'}else{'false'}
                $entries.Add("{$(Quote $l.property),$($min.ToString('R',[Globalization.CultureInfo]::InvariantCulture)),$($max.ToString('R',[Globalization.CultureInfo]::InvariantCulture)),$color,$area},")
            }
            $lines.Add("static const GraphLine graph_$index`[]={"+($entries -join '')+'};')
            $graphs.Add("{$id,$(Quote $g.header),graph_$index,$($entries.Count)},");$index++
        }
    }
    $lines.Add('static const Rule rules[]={'+($rules -join '')+'};')
    $lines.Add('constexpr unsigned RuleCount=sizeof(rules)/sizeof(rules[0]);')
    $lines.Add('static const Graph graphs[]={'+($graphs -join '')+'};')
    $lines.Add('constexpr unsigned GraphCount=sizeof(graphs)/sizeof(graphs[0]);')
    $lines.Add('static const FilterList filterLists[]={'+($filters -join '')+'};')
    $lines.Add('constexpr unsigned FilterListCount=sizeof(filterLists)/sizeof(filterLists[0]);');$lines.Add('}')
    $lines.Add('#include "runtime_experiments.h"')
    [IO.File]::WriteAllLines($Output,$lines,(New-Object Text.UTF8Encoding($false)))
    Write-Output "Generated $($rules.Count) advisory identification rules and $($graphs.Count) graphs"
    
}

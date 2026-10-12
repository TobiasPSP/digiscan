#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param()
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$output = Join-Path $project '.pio/profile-step-tests'
$profiles = Join-Path $output 'profiles'
$null = New-Item -ItemType Directory -Path $profiles -Force
$yamlModule = Join-Path $project 'tools/vendor/powershell-yaml/0.4.12/powershell-yaml.psd1'
Import-Module $yamlModule -Force
$source = Get-Content -LiteralPath (Join-Path $project 'profiles/ip2366.yaml') -Raw -Encoding utf8
$builder = Join-Path $project 'tools/Build-Profiles.ps1'
$header = Join-Path $output 'profiles.h'
$fixture = Join-Path $profiles 'ip2366.yaml'
$utf8 = New-Object Text.UTF8Encoding($false)
$doc = ConvertFrom-Yaml -Yaml $source -Ordered
foreach ($property in $doc.properties) {
    if ($property.friendlydesc -isnot [string] -or !$property.friendlydesc.Trim()) { throw ('Friendly description missing: ' + $property.id) }
}
$steps = @($doc.properties | Where-Object { $_.Contains('atomic_step') })
if ($steps.Count -ne 7) { throw 'Expected seven documented numeric steps.' }
foreach ($property in $steps) {
    if (!$property.atomic_step_basis) { throw ('Step rationale missing: ' + $property.id) }
}
[IO.File]::WriteAllText($fixture,$source,$utf8)
& $builder -ProfileDirectory $profiles -Output $header
if (!(Test-Path -LiteralPath $header)) { throw 'Generation failed.' }
foreach ($case in @('negative','string','boolean','enum','ascii','conflict')) {
    Import-Module $yamlModule -Force
    $doc = ConvertFrom-Yaml -Yaml $source -Ordered
    $p = $doc.properties | Where-Object { $_.id -eq 'battery_voltage_mv' }
    if ($case -eq 'negative') { $p.atomic_step = -1 }
    elseif ($case -eq 'string') { $p.atomic_step = '100mV' }
    else {
        $id = switch ($case) { 'boolean' { 'r17_ntc_en' } 'enum' { 'r22_vbus_mode_set' } 'ascii' { 'identification_1' } 'conflict' { 'r33_chg_vbus' } }
        $p = $doc.properties | Where-Object { $_.id -eq $id }
        $p['atomic_step'] = 1
    }
    [IO.File]::WriteAllText($fixture,(ConvertTo-Yaml -Data $doc),$utf8)
    $rejected = $false
    try { & $builder -ProfileDirectory $profiles -Output $header } catch { $rejected = $true }
    if (!$rejected) { throw ('Invalid atomic_step accepted: ' + $case) }
}
Write-Host 'PASS: seven documented steps, WPS 5.1 generation and invalid-step schema rejection'
foreach ($case in @('missing','multiline','number','oversize')) {
    Import-Module $yamlModule -Force
    $doc = ConvertFrom-Yaml -Yaml $source -Ordered
    $p = $doc.properties[0]
    if ($case -eq 'missing') { $p.Remove('friendlydesc') }
    elseif ($case -eq 'multiline') { $p.friendlydesc = " Friendly explanation.`nSecond line. " }
    elseif ($case -eq 'number') { $p.friendlydesc = 42 }
    else { $p.friendlydesc = 'x' * 2049 }
    [IO.File]::WriteAllText($fixture,(ConvertTo-Yaml -Data $doc),$utf8)
    $rejected = $false
    try { & $builder -ProfileDirectory $profiles -Output $header } catch { $rejected = $true }
    if ($rejected -ne ($case -in @('number','oversize'))) { throw ('Incorrect friendlydesc validation: ' + $case) }
}
Write-Host 'PASS: all friendly descriptions present; optional/multiline supported; invalid metadata rejected'

#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $project 'tools/Interpreter.psm1') -Force
foreach ($file in Get-ChildItem (Join-Path $project 'profiles') -Filter '*.yaml') {
    $profile=Import-DeviceProfile $file.FullName
    foreach ($item in $profile.Properties) {
        $definition=$item.Definition
        $binary=($item.Mask -shr $item.Shift) -eq 1
        $flagText=([string]$definition.friendlydesc + ' ' + [string]$definition.description) -match '\bWhen 1\b'
        if ($binary -and $flagText -and !$item.Enum.Count -and $item.Scale -eq 1 -and $item.Offset -eq 0 -and !$item.BitMeanings.Count) {
            throw "Missing boolean metadata: $($file.Name) / $($definition.id)"
        }
        if ($item.BitMeanings.Count -and ($item.Enum.Count -or $item.Scale -ne 1 -or $item.Offset -ne 0)) {
            throw "Boolean classification hides an assigned value: $($file.Name) / $($definition.id)"
        }
    }
}
$out=Join-Path $project '.pio/profile-tests'
$null=New-Item -ItemType Directory -Path $out -Force
$exe=Join-Path $out 'profiles.exe'
& $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-I' (Join-Path $project 'include') (Join-Path $PSScriptRoot 'test_profiles.cpp') '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Profile tests compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Profile tests failed.' }

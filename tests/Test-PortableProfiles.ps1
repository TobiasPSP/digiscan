#requires -Version 5.1
[CmdletBinding(PositionalBinding=$false)]
param([string]$Compiler='C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
foreach($id in @('ip2366','ip2368','ip2369')){& (Join-Path $project 'tools/Convert-Profile.ps1') -Yaml (Join-Path $project "profiles/$id.yaml")}
$out=Join-Path $project '.pio/portable-tests';$null=New-Item -ItemType Directory -Path $out -Force
$exe=Join-Path $out 'portable.exe'
& $Compiler '-std=c++17' '-I' (Join-Path $project 'include') '-I' $PSScriptRoot (Join-Path $PSScriptRoot 'test_portable.cpp') '-o' $exe
if($LASTEXITCODE -ne 0){throw 'Portable tests compilation failed.'}
& $exe (Join-Path $project 'profiles')
if($LASTEXITCODE -ne 0){throw 'Portable profile tests failed.'}

#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
$out=Join-Path $project '.pio/report-tests'
$null=New-Item -ItemType Directory -Path $out -Force
$exe=Join-Path $out 'reports.exe'
& $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-I' (Join-Path $project 'include') (Join-Path $PSScriptRoot 'test_reports.cpp') '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Report compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Report tests failed.' }

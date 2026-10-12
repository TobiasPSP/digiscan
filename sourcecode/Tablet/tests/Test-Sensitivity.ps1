#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$output = Join-Path $project '.pio/sensitivity-tests'
$null = New-Item -ItemType Directory -Path $output -Force
$exe = Join-Path $output 'sensitivity.exe'
& $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-I' (Join-Path $project 'include') (Join-Path $PSScriptRoot 'test_sensitivity.cpp') '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Sensitivity compilation failed.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Sensitivity tests failed.' }

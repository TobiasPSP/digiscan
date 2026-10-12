#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$instrument = Join-Path (Split-Path $project -Parent) 'rp2350-sniffer-v12'
if ((Get-FileHash (Join-Path $project 'include/transport_v5.h')).Hash -ne (Get-FileHash (Join-Path $instrument 'src/transport_v5.h')).Hash) { throw 'Shared transport copies differ.' }
$output = Join-Path $project '.pio/transport-tests'
$null = New-Item -ItemType Directory -Path $output -Force
$exe = Join-Path $output 'test_transport.exe'
& $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-I' (Join-Path $project 'include') (Join-Path $PSScriptRoot 'test_transport.cpp') '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'Transport tests failed to compile.' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Transport tests failed.' }

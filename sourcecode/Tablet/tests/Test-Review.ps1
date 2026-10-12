#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$instrument = Join-Path (Split-Path $project -Parent) 'rp2350-sniffer-v7'
$output = Join-Path $project '.pio/review-tests'
$null = New-Item -ItemType Directory -Path $output -Force
$tests = @((Join-Path $PSScriptRoot 'test_review.cpp'))
foreach ($name in @('test_client_protocol','test_long_transfers','test_qualification','test_timing','test_delivery')) {
    $tests += Join-Path $instrument ('tests/' + $name + '.cpp')
}
foreach ($source in $tests) {
    $exe = Join-Path $output (([IO.Path]::GetFileNameWithoutExtension($source)) + '.exe')
    & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-I' (Join-Path $project 'include') $source '-o' $exe
    if ($LASTEXITCODE -ne 0) { throw "Review test compilation failed: $source" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "Review test failed: $source" }
}

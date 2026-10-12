[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')

$project = Split-Path $PSScriptRoot -Parent
if (!(Test-Path -LiteralPath $Compiler)) { throw "C++ compiler not found: $Compiler" }
$output = Join-Path $env:TEMP ('tablet-protocol-' + [guid]::NewGuid().ToString('N') + '.exe')
try {
    & $Compiler '-std=c++17' '-Wall' '-Wextra' '-Werror' '-I' (Join-Path $project 'include') (Join-Path $PSScriptRoot 'test_protocol.cpp') '-o' $output
    if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Native protocol/touch tests failed.' }
} finally {
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output }
}

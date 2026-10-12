[CmdletBinding(PositionalBinding = $false)]
param([string]$Compiler = 'C:\Program Files\LLVM\bin\clang++.exe')

$project = Split-Path $PSScriptRoot -Parent
$lvgl = Join-Path $project '.pio\libdeps\tablet\lvgl'
if (!(Test-Path -LiteralPath $Compiler)) { throw "Compiler not found: $Compiler" }
if (!(Test-Path -LiteralPath $lvgl)) { throw 'Run the PlatformIO build first to install LVGL.' }
$output = Join-Path $project '.pio\native-preview'
$null = New-Item -ItemType Directory -Path $output -Force
$exe = Join-Path $output 'preview.exe'
$response = Join-Path $output 'build.rsp'
$arguments = @('-w', '-DLV_CONF_INCLUDE_SIMPLE', '-DTABLET_PREVIEW')
foreach ($path in @((Join-Path $project 'include'), $PSScriptRoot, $lvgl)) {
    $arguments += '-I"' + $path.Replace('\','/') + '"'
}
$arguments += '-x c'
foreach ($file in (Get-ChildItem -LiteralPath (Join-Path $lvgl 'src') -Recurse -Filter '*.c')) {
    $arguments += '"' + $file.FullName.Replace('\','/') + '"'
}
$arguments += @('-x c++', ('"' + (Join-Path $PSScriptRoot 'preview.cpp').Replace('\','/') + '"'), '-o', ('"' + $exe.Replace('\','/') + '"'))
[IO.File]::WriteAllLines($response, [string[]]$arguments, (New-Object Text.UTF8Encoding($false)))
& $Compiler ('@' + $response)
if ($LASTEXITCODE -ne 0) { throw 'LVGL preview compilation failed.' }
& $exe $output
if ($LASTEXITCODE -ne 0) { throw 'LVGL layout tests failed.' }
Write-Host "Rendered PPM screenshots: $output"

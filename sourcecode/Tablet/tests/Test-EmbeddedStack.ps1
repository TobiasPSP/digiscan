[CmdletBinding(PositionalBinding = $false)]
param([string]$Elf = $null, [string]$Objdump = $null)

$project = Split-Path $PSScriptRoot -Parent
if (!$Elf) { $Elf = Join-Path $project '.pio\build\tablet\firmware.elf' }
if (!$Objdump) { $Objdump = Join-Path $env:USERPROFILE '.platformio\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-objdump.exe' }
if (!(Test-Path -LiteralPath $Elf)) { throw "Build the tablet first: $Elf" }
if (!(Test-Path -LiteralPath $Objdump)) { throw "Objdump not found: $Objdump" }
$disassembly = & $Objdump -d -C $Elf
if ($LASTEXITCODE -ne 0) { throw 'ESP32 disassembly failed.' }
$frames = @{}
$symbol = $null
foreach ($line in $disassembly) {
    if ($line -match '^\w+ <(.+)>:$') { $symbol = $Matches[1]; continue }
    if ($symbol -and $line -match '\bentry\s+a1,\s+(0x[0-9a-f]+|\d+)') {
        $number = $Matches[1]
        $bytes = if ($number.StartsWith('0x')) { [Convert]::ToInt32($number.Substring(2),16) } else { [int]$number }
        $frames[$symbol] = $bytes
        $symbol = $null
    }
}
$limits = @{
    '(anonymous namespace)::refreshRecorder()' = 1024
    '(anonymous namespace)::recorderPostDialog()' = 1024
    '(anonymous namespace)::refreshUi()' = 3072
    '(anonymous namespace)::pump()' = 1024
    # burstTick is inlined into pump; its storage is covered by pump's guard.
    '(anonymous namespace)::burstStart((anonymous namespace)::BurstOwner, unsigned int const*, unsigned int)' = 1024
    '(anonymous namespace)::burstStop()' = 512
    'recording18::Session::observe(observer::Observation const&, recorder_wire::Timing const*, unsigned int)' = 1024
    'setup()' = 1024
    'loop()' = 1024
    'portable::Package::parse(unsigned char*, unsigned int)' = 1024
    'portable::Disk::begin()' = 1024
    'portable::Disk::read(char const*)' = 1024
    'portable::Disk::commit(portable::Package&)' = 1024
    '(anonymous namespace)::startClearLocal()' = 1024
}
foreach ($name in $limits.Keys) {
    if (!$frames.ContainsKey($name)) { throw "Missing ESP32 stack prologue: $name" }
    $bytes = $frames[$name]
    if ($bytes -gt $limits[$name]) { throw "Excessive embedded stack frame: $name = $bytes bytes (limit $($limits[$name]))" }
    Write-Host "PASS: $name stack frame = $bytes bytes"
}
Write-Host 'Static frame guard only; nested library calls and live stack high-water still require hardware validation.'

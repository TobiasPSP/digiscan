#requires -Version 5.1
[CmdletBinding(PositionalBinding=$false)]
param()
$ErrorActionPreference='Stop'
Import-Module (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools/Interpreter.psm1') -Force
$profile=Import-DeviceProfile (Join-Path (Split-Path $PSScriptRoot -Parent) 'profiles/ip2368.yaml')
if ($profile.Properties.Count -ne 86) { throw 'Unexpected IP2368 property count.' }
$state=New-InterpreterState $profile
function Read-Byte([int]$Reg,[int]$Value,[bool]$Ok=$true) {
    $source='0x75 Reg 0x{0:X2} Read {1:X2} {2}' -f $Reg,$Value,$(if ($Ok) {'OK'} else {'FAIL'})
    Invoke-InputLine $state $source
}
$output=@(Read-Byte 0x74 0x40;Read-Byte 0x75 0x0D)
if ($output.Count -or $null -eq $state.Pending) { throw 'Power completed before the third byte.' }
$output=@(Read-Byte 0x76 3) -join "`n"
if ($output -notmatch '200000 mW' -or $null -ne $state.Pending) { throw "Incorrect 24-bit power output: $output" }
$output=@(Read-Byte 0x50 0x33;Read-Byte 0x51 0x34) -join "`n"
if ($output -notmatch '13363 mV') { throw 'Two-byte regression.' }
$output=@(Read-Byte 0x74 1;Read-Byte 0x75 2 $false;Read-Byte 0x76 3) -join "`n"
if ($output -notmatch 'INCOMPLETE' -or $output -match '197121 mW') { throw 'Failed middle byte was accepted.' }
$output=@(Read-Byte 0x74 0xFF;Read-Byte 0x75 0xFF;Read-Byte 0x76 0xFF) -join "`n"
if ($output -notmatch '16777215 mW') { throw '24-bit maximum truncated.' }
Write-Host 'PASS: Windows PowerShell 5.1 IP2368 schema, three-byte completion, full range, failed component and two-byte replay regression.'

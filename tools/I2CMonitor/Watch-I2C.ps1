<#
.SYNOPSIS
Operate an I2C sniffer from the PC and decode transactions with a YAML profile.
.DESCRIPTION
Uses the RP2350 native USB text console; no tablet is needed. Omitted Port selects
automatic Windows COM discovery, with a prompt if ambiguous. Omitted InputFile
selects live mode; InputFile selects replay. The current IP2366 definition and
YAML dependencies are bundled. Start live capture with S; P pauses and Q exits.
Recording starts capture automatically and pauses it before closing the port.
.PARAMETER Port
Named Windows COM port of the RP instrument, not the tablet or UART bridge.
.PARAMETER Profile
YAML file. Omit to use the bundled IP2366 profile. Relative paths use the caller's location.
.PARAMETER InputFile
Recorded text file for offline replay; omit for live mode. Cannot be combined with Port.
.PARAMETER ListPorts
List available Windows COM ports without opening a connection.
.PARAMETER ValidateProfile
Validate the selected profile without opening a connection.
.PARAMETER InputFormat
Readable transaction lines (default) or legacy Raw frame lines.
.PARAMETER Baud
SerialPort baud setting, default 115200. Native USB throughput is not limited by this setting.
.PARAMETER RecordPath
Record incoming text. Prompts before overwrite; recording automatically starts capture.
.PARAMETER RecordTime
Maximum recording duration in seconds. Omit for unlimited duration.
.PARAMETER MaxRecordItems
Maximum recorded event count. If combined with RecordTime, the first limit wins.
.PARAMETER ParsedLog
File receiving interpreted output.
.PARAMETER MaxSeconds
Maximum live session duration; zero means unlimited.
.PARAMETER NoColor
Disable console output colors.
.PARAMETER InvestigateAddress
Hexadecimal address for an initial investigation selection.
.PARAMETER InvestigateRegisters
Register selection accepted by the investigation console, for example 0x22,0x34.
.PARAMETER ShowInventory
Show observed register inventory when the session ends.
.PARAMETER ShowFailures
Show failure summaries when the session ends.
.PARAMETER ShowRemark
Include profile inference remarks in interpreted output.
.PARAMETER Force
Overwrite output files without prompting. Does not bypass profile validation.
.PARAMETER View
All, Known or Unknown register view, with firmware-side filtering when supported.
.PARAMETER Outcome
Both, Success or Failure transaction results.
.PARAMETER Qualification
Request pulse qualification On or Off. Omit to retain the firmware setting.
.EXAMPLE
Watch-I2C
.EXAMPLE
Watch-I2C -Port COM7 -RecordPath .\logs\capture.txt -RecordTime 60
.EXAMPLE
Watch-I2C -InputFile .\logs\capture.txt -ShowFailures
.NOTES
Supports Windows PowerShell 5.1. Keep the whole module folder together. Close other
serial monitors. If USB was switched to structured mode, reset the RP first.
Do not use a tablet and this console simultaneously to control the same capture.
The console controls passive capture only; it does not issue active I2C operations.
#>
function Watch-I2C {
    [CmdletBinding(PositionalBinding=$false)]
    param(
        [string]$Port=$null,
        [string]$InputFile=$null,
        [switch]$ListPorts,
        [switch]$ValidateProfile,
        [string]$Profile=$null,
        [ValidateSet('Readable','Raw')][string]$InputFormat='Readable',
        [ValidateRange(9600,921600)][int]$Baud=115200,
        [string]$RecordPath,
        [ValidateRange(0,2147483647)][int]$RecordTime=0,
        [ValidateRange(0,9223372036854775807)][long]$MaxRecordItems=0,
        [string]$ParsedLog,
        [ValidateRange(0,86400)][int]$MaxSeconds=0,
        [switch]$NoColor,
        [string]$InvestigateAddress=$null,
        [string]$InvestigateRegisters=$null,
        [switch]$ShowInventory,
        [switch]$ShowFailures,
        [switch]$ShowRemark,
        [switch]$Force,
        [ValidateSet('All','Known','Unknown')][string]$View='All',
        [ValidateSet('Both','Success','Failure')][string]$Outcome='Both',
        [ValidateSet('','On','Off')][string]$Qualification=$null
    )
    # Preserve explicit parameter binding, including omitted recording limits.
    # Run in child script scope so console helpers do not replace module helpers.
    & (Join-Path $PSScriptRoot 'Console/Watch-I2C.ps1') @PSBoundParameters
}

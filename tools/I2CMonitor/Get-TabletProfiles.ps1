<#
.SYNOPSIS
Return installed tablet chip definitions and their versions.
.DESCRIPTION
Uses the bundled YAML parser and validates the profile before producing output.
Supports Windows PowerShell 5.1. Paths resolve against the caller's current
PowerShell location. Serial functions discover ports when Port is omitted and
ask for a selection when detection is ambiguous. No module download is needed.
.EXAMPLE
Get-TabletProfiles -Port COM18
.PARAMETER Port
Tablet COM port, or omit to detect candidates and choose when ambiguous.
.OUTPUTS
System.Management.Automation.PSCustomObject with Chip, Name, Revision, Source, Crc.
.NOTES
UART to the RP2350 is not the upload interface: use the tablet's USB COM port.
Active capture is paused by profile installation; inventory is read-only.
#>
function Get-TabletProfiles {
    [CmdletBinding(PositionalBinding=$false)]
    param([string]$Port)
    $ErrorActionPreference='Stop'
    $link=Open-ProfilePort $Port
    # Return data objects, not Format-Table records, for pipeline composability.
    try { Get-ProfileInventory $link }
    finally{$link.Serial.Close();$link.Serial.Dispose()}
    
}

<#
.SYNOPSIS
Validate and upload a YAML profile to the tablet over USB serial.
.DESCRIPTION
Uses the bundled YAML parser and validates the profile before producing output.
Supports Windows PowerShell 5.1. Paths resolve against the caller's current
PowerShell location. Serial functions discover ports when Port is omitted and
ask for a selection when detection is ambiguous. No module download is needed.
.EXAMPLE
Publish-Profile .\profiles\ip2366.yaml -Port COM18
.PARAMETER Yaml
Path to the human-readable chip profile; validated before opening a serial port.
.PARAMETER Port
Tablet COM port. Omit for automatic detection; this parameter is never positional.
.PARAMETER Force
Permit replacement without a confirmation prompt. Still warns and validates.
.NOTES
UART to the RP2350 is not the upload interface: use the tablet's USB COM port.
Active capture is paused by profile installation; inventory is read-only.
#>
function Publish-Profile {
    [CmdletBinding(PositionalBinding=$false)]
    param([Parameter(Position=0)][string]$Yaml,[string]$Port,[switch]$Force)
    $ErrorActionPreference='Stop'
    if(!$Yaml){throw 'Specify a YAML path: Publish-Profile .\profiles\ip2366.yaml'}
    # Validate before contacting hardware: malformed YAML must not pause capture.
    $package=Convert-PortableProfile $Yaml
    Write-Host ('Validated {0}, revision {1}, {2} bytes.' -f $package.Name,$package.Revision,$package.Bytes.Length)
    $link=Open-ProfilePort $Port;$begun=$false;$committed=$false;$commitRequested=$false
    try {
        # Chip ID, not version, determines replacement. Downgrades/same-version
        # uploads require the same confirmation as upgrades.
        $inventory=@(Get-ProfileInventory $link);$existing=@($inventory | Where-Object Chip -eq $package.Id)
        if($existing.Count){
            Write-Warning ('{0} already exists: {1}, revision {2} ({3}). Incoming: {4}, revision {5}. Replacement warning applies even to the same version.' -f $package.Id,$existing[0].Name,$existing[0].Revision,$existing[0].Source,$package.Name,$package.Revision)
            if(!$Force -and (Read-Host 'Upload and replace this chip definition? [y/N]') -notmatch '^(y|yes)$'){Write-Host 'Canceled; no changes made.';return}
        }
        Write-Host 'Capture will be paused. Successful installation restarts the tablet on START with automatic passive discovery.'
        $begun=$true
        Send-ProfileCommand $link ('BEGIN {0} {1} {2:X8} {3}' -f $package.Id,$package.Bytes.Length,$package.Crc,[int]($existing.Count -gt 0))
        if((Receive-ProfileReply $link 20) -ne 'READY'){throw 'Tablet did not become ready.'}
        # Each 512-byte chunk is acknowledged at the resulting byte offset.
        # Do not retry an unexpected ACK: the tablet's state may be uncertain.
        for($offset=0;$offset -lt $package.Bytes.Length;){
            $length=[Math]::Min(512,$package.Bytes.Length-$offset)
            $hex=[BitConverter]::ToString($package.Bytes,$offset,$length).Replace('-','')
            Send-ProfileCommand $link "DATA $offset $hex"
            $offset+=$length
            if((Receive-ProfileReply $link) -ne "ACK $offset"){throw 'Unexpected chunk acknowledgement.'}
            Write-Progress -Activity "Uploading $($package.Name)" -Status "$offset / $($package.Bytes.Length) bytes" -PercentComplete (100*$offset/$package.Bytes.Length)
        }
        # COMMITTED verifies durable installation, not merely UART delivery.
        $commitRequested=$true
        Send-ProfileCommand $link 'COMMIT'
        $expected='COMMITTED {0} {1} {2:X8}' -f $package.Id,$package.Revision,$package.Crc
        if((Receive-ProfileReply $link 30) -ne $expected){throw 'Unexpected install confirmation.'};$committed=$true
        Write-Host "Verified on tablet: $expected"
        Write-Host 'Tablet is restarting. The installed profile and address assignments are loaded on boot.'
    } finally {
        # Always release the COM handle. An unacknowledged commit must be checked
        # with inventory, since it may have succeeded immediately before reboot.
        Write-Progress -Activity "Uploading $($package.Name)" -Completed
        if($begun -and !$committed){
            try{Send-ProfileCommand $link 'ABORT'}catch{}
            if($commitRequested){Write-Warning 'Installation outcome is uncertain. After the tablet restarts, run Get-TabletProfiles before retrying.'}
            else{Write-Warning 'Upload was not committed. Previous installed definitions remain intact; check capture state before resuming.'}
        }
        $link.Serial.Close();$link.Serial.Dispose()
    }
    
}

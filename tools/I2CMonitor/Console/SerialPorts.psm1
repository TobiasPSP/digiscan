#requires -Version 5.1
$ErrorActionPreference='Stop'

function Convert-PortInventory {
    param([string[]]$PortNames=@(),[object[]]$Devices=@())
    $metadata=@{}
    foreach ($device in $Devices) {
        if (!$device.Present -or [string]$device.Name -notmatch '\((COM\d+)\)\s*$') { continue }
        $port=$Matches[1].ToUpperInvariant()
        if (!$metadata.ContainsKey($port)) { $metadata[$port]=[Collections.Generic.List[object]]::new() }
        $metadata[$port].Add($device)
    }
    foreach ($port in @($PortNames | Where-Object { $_ -match '^COM[1-9]\d*$' } | ForEach-Object { $_.ToUpperInvariant() } | Sort-Object -Unique | Sort-Object { [long]$_.Substring(3) })) {
        $name='Device information unavailable'; $id=''; $candidate=$false
        # Multiple contradictory metadata entries must not qualify for automatic selection.
        if ($metadata.ContainsKey($port) -and $metadata[$port].Count -eq 1) {
            $device=$metadata[$port][0]
            $name=[string]$device.Name; $id=[string]$device.PNPDeviceID
            # Configured RP2040-Zero and headless RP2350 USB identities.
            # This identifies a board family, not proof that sniffer firmware is loaded.
            $candidate=$id -match '^USB\\VID_2E8A&PID_(?:0003|F00F)(?:&|\\|$)'
        }
        [pscustomobject]@{Port=$port;Description=$name;LikelySniffer=$candidate;DeviceId=$id}
    }
}

function Get-SnifferPorts {
    $ports=@([IO.Ports.SerialPort]::GetPortNames())
    $devices=@()
    if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT -and $ports.Count) {
        try {
            $devices=@(Get-CimInstance -ClassName Win32_PnPEntity -Filter "PNPClass='Ports'" -OperationTimeoutSec 5 -ErrorAction Stop)
        } catch { Write-Warning 'USB device information unavailable; manual port selection will be offered.' }
    }
    Convert-PortInventory -PortNames $ports -Devices $devices
}

function Select-SnifferPort {
    param([object[]]$Ports=@(),[string]$RequestedPort,[scriptblock]$ReadChoice)
    if (!$Ports.Count) { throw 'No COM ports found. Connect the RP2040/RP2350 running the sniffer, not BOOTSEL USB-drive mode, then retry.' }
    if ($RequestedPort) {
        $match=@($Ports | Where-Object { $_.Port -eq $RequestedPort })
        if ($match.Count -ne 1) { throw "Port $RequestedPort is not currently listed. Run -ListPorts." }
        return $match[0].Port
    }
    $candidates=@($Ports | Where-Object { $_.LikelySniffer })
    if ($candidates.Count -eq 1) {
        Write-Host "Auto-selected $($candidates[0].Port): $($candidates[0].Description) (configured sniffer board USB identity)."
        return $candidates[0].Port
    }
    Write-Host $(if ($candidates.Count -gt 1) {'Multiple likely RP2040/RP2350 boards found; choose the sniffer.'} else {'No uniquely identified sniffer found; choose its COM port.'})
    for ($i=0;$i -lt $Ports.Count;$i++) {
        $hint=if ($Ports[$i].LikelySniffer) {' [likely sniffer board]'} else {''}
        Write-Host ('{0}. {1} - {2}{3}' -f ($i+1),$Ports[$i].Port,$Ports[$i].Description,$hint)
    }
    if (!$ReadChoice) {
        if ([Console]::IsInputRedirected) { throw 'Port selection requires an interactive terminal. Specify -Port COM7 explicitly.' }
        $ReadChoice={ param($Prompt) Read-Host $Prompt }
    }
    while ($true) {
        $answer=& $ReadChoice 'Select number or COM name (Q cancels)'
        if ($null -eq $answer -or [string]$answer -match '^\s*q\s*$') { throw 'Port selection cancelled.' }
        $answer=([string]$answer).Trim()
        $chosen=@($Ports | Where-Object { $_.Port -eq $answer })
        if ($chosen.Count -eq 1) { return $chosen[0].Port }
        [int]$number=0
        if ([int]::TryParse($answer,[ref]$number) -and $number -ge 1 -and $number -le $Ports.Count) { return $Ports[$number-1].Port }
        Write-Host 'Invalid selection. Enter a listed number or COM name.' -ForegroundColor Yellow
    }
}

Export-ModuleMember -Function Get-SnifferPorts,Convert-PortInventory,Select-SnifferPort

<#
.SYNOPSIS
Validate YAML and create a portable IPRF binary profile.
.DESCRIPTION
Uses the bundled YAML parser and validates the profile before producing output.
Supports Windows PowerShell 5.1. Paths resolve against the caller's current
PowerShell location. Serial functions discover ports when Port is omitted and
ask for a selection when detection is ambiguous. No module download is needed.
.EXAMPLE
Convert-Profile .\profiles\ip2366.yaml
.PARAMETER Yaml
Input YAML path. Relative paths use the caller's current PowerShell directory.
.PARAMETER Output
Output binary package path; defaults to the input basename with .i2cp extension.
.NOTES
UART to the RP2350 is not the upload interface: use the tablet's USB COM port.
Active capture is paused by profile installation; inventory is read-only.
#>
function Convert-Profile {
    [CmdletBinding(PositionalBinding=$false)]
    param([Parameter(Position=0)][string]$Yaml,[string]$Output)
    $ErrorActionPreference='Stop'
    if(!$Yaml){throw 'Specify a YAML path: Convert-Profile .\profiles\ip2366.yaml'}
    # Serialization validates references and bounds before touching output files.
    $package=Convert-PortableProfile $Yaml
    if(!$Output){$Output=[IO.Path]::ChangeExtension($package.Path,'.i2cp')}
    $Output=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
    $null=New-Item -ItemType Directory -Path (Split-Path $Output -Parent) -Force
    [IO.File]::WriteAllBytes($Output,$package.Bytes)
    Write-Host ('Validated {0}, revision {1}, {2} properties, {3} bytes -> {4}' -f $package.Name,$package.Revision,$package.Properties,$package.Bytes.Length,$Output)
    
}

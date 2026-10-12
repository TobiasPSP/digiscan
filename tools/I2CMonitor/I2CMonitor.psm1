# Load the validated codec and its bundled YAML dependency into module scope.
# Only the six user-facing advanced functions are exported. Serial framing,
# schema validation and binary serialization helpers remain implementation details.
Import-Module (Join-Path $PSScriptRoot 'PortableProfile.psm1') -ErrorAction Stop
foreach ($name in @('New-ExperimentHeader','New-ProfileHeaders','New-MonitorFont',
                    'Convert-Profile','Publish-Profile','Get-TabletProfiles')) {
    . (Join-Path $PSScriptRoot ($name + '.ps1'))
}
Export-ModuleMember -Function Convert-Profile,Publish-Profile,Get-TabletProfiles,
    New-ProfileHeaders,New-ExperimentHeader,New-MonitorFont

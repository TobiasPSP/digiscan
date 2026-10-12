[CmdletBinding(PositionalBinding = $false)]
param()
foreach ($name in @('Test-Protocol.ps1', 'Test-Reports.ps1', 'Test-Transport.ps1', 'Test-Profiles.ps1')) {
    & (Join-Path $PSScriptRoot $name)
    if ($LASTEXITCODE -ne 0) { throw "Regression failed: $name" }
}

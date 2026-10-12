#requires -Version 5.1
[CmdletBinding(PositionalBinding = $false)]
param()
$ErrorActionPreference='Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'tools/ReportSchema.ps1')
function Assert-Rejected($Data) {
    $rejected=$false
    try { $null=Get-ReportSchema $Data } catch { $rejected=$true }
    if (!$rejected) { throw 'Invalid report schema accepted.' }
}
$valid=[ordered]@{reports=@([ordered]@{header='All';groups=@([ordered]@{header=$null;items=@('All','0x50_1111111111111111')})})}
if (@(Get-ReportSchema $valid).Count -ne 1) { throw 'Valid report not accepted.' }
foreach ($item in @('ALL','0x50_00000000','0x50_111','0x50_1111111Z')) {
    Assert-Rejected ([ordered]@{reports=@([ordered]@{header='All';groups=@([ordered]@{header=$null;items=@($item)})})})
}
Assert-Rejected ([ordered]@{reports=@([ordered]@{header='';groups=@()})})
Assert-Rejected ([ordered]@{reports=@([ordered]@{header='All';extra='bad';groups=@([ordered]@{header=$null;items=@('All')})})})
if (@(Get-ReportSchema ([ordered]@{})).Count) { throw 'Absent reports should be allowed.' }
Write-Host 'PASS: Windows PowerShell report schema validation; structural references remain unresolved.'

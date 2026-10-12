#requires -Version 5.1
[CmdletBinding(PositionalBinding=$false)]
param()
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
Import-Module (Join-Path $project 'tools/PortableProfile.psm1') -Force
if([PortableCrc]::Compute([Text.Encoding]::ASCII.GetBytes('123456789'),0,9) -ne 0xCBF43926L){throw 'CRC32 reference mismatch.'}
Add-Type -TypeDefinition @'
public class ProfileMockSerial {
    public string Pending=""; public string Sent="";
    public void WriteLine(string line){Sent+=line+"\n";}
    public string ReadExisting(){string s=Pending;Pending="";return s;}
}
'@
$serial=New-Object ProfileMockSerial
$link=[pscustomobject]@{Serial=$serial;Token='aabb1122';Partial='';Port='MOCK'}
$serial.Pending="boot noise`nPRF1 deadbeef TABLET V20 IPRF1 storage_ready`nPRF1 aabb1122 TABLET V20 IPRF1 storage_ready`nPRF1 aabb1122 CHIP ip2366 2 AABBCCDD uploaded IP2366 v1.0`nPRF1 aabb1122 END`n"
$inventory=@(Get-ProfileInventory $link)
if($inventory.Count -ne 1 -or $inventory[0].Chip -ne 'ip2366' -or $inventory[0].Revision -ne 2 -or $inventory[0].Source -ne 'uploaded'){throw 'Batched inventory reception failed.'}
$serial.Pending='PRF1 aabb';$link.Partial='';$serial.Pending+="1122 ACK 512`n"
if((Receive-ProfileReply $link) -ne 'ACK 512'){throw 'Chunk ACK mismatch.'}
$serial.Pending="PRF1 aabb1122 ERROR bad_chunk`n";$rejected=$false
try{$null=Receive-ProfileReply $link}catch{$rejected=$_.Exception.Message -match 'bad_chunk'}
if(!$rejected){throw 'Firmware errors were not propagated.'}
$baseline=Convert-PortableProfile (Join-Path $project 'profiles/ip2366.yaml')
$root=Join-Path $project '.pio/portable-script-tests';$null=New-Item -ItemType Directory -Path $root -Force
$yaml=Get-Content -LiteralPath $baseline.Path -Raw -Encoding UTF8
foreach($change in @(
    @{Name='bad-mask';Text=$yaml.Replace('mask: 0x80','mask: 0x81')},
    @{Name='bad-report';Text=$yaml.Replace('0x50_1111111111111111','0xFE_1111111111111111')},
    @{Name='bad-protocol';Text=$yaml.Replace('clock_Hz: 10000','clock_Hz: 400000')},
    @{Name='bad-step';Text=$yaml.Replace('atomic_step: 100','atomic_step: -100')}
)){
    $path=Join-Path $root ($change.Name+'.yaml');[IO.File]::WriteAllText($path,$change.Text,(New-Object Text.UTF8Encoding($false)))
    $failed=$false;try{$null=Convert-PortableProfile $path}catch{$failed=$true};if(!$failed){throw "Accepted invalid schema: $($change.Name)"}
}
$sample=Join-Path $root 'edited.yaml'
[IO.File]::WriteAllText($sample,$yaml.Replace('name: IP2366 v1.0','name: IP2366 v1.1').Replace('profile_revision: 2','profile_revision: 3'),(New-Object Text.UTF8Encoding($false)))
$edited=Convert-PortableProfile $sample
if($edited.Name -ne 'IP2366 v1.1' -or $edited.Revision -ne 3 -or $edited.Crc -eq $baseline.Crc){throw 'Edited YAML metadata not converted.'}
foreach($file in @('Publish-Profile.ps1','Get-TabletProfiles.ps1','Convert-Profile.ps1')){
    $command=Get-Command (Join-Path $project "tools/$file")
    if($command.Parameters.ContainsKey('Port')){
        foreach($attribute in $command.Parameters.Port.Attributes){if($attribute -is [Management.Automation.ParameterAttribute] -and $attribute.Position -ne [int]::MinValue){throw 'Port must not be positional.'}}
    }
}
Write-Host 'PASS: Windows PowerShell 5.1 CRC, complete/batched replies, token isolation, errors, edited YAML, schema rejection and named-only Port.'

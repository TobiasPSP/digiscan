@{
    RootModule = 'I2CMonitor.psm1'
    ModuleVersion = '1.0.0'
    GUID = 'cb31fdd7-8fbf-4957-b798-6f67589fa18c'
    Author = 'I2C Monitor contributors'
    Description = 'Self-contained tablet profile conversion, installation and maintenance tools.'
    PowerShellVersion = '5.1'
    FunctionsToExport = @('Convert-Profile','Publish-Profile','Get-TabletProfiles',
        'New-ProfileHeaders','New-ExperimentHeader','New-MonitorFont')
    CmdletsToExport = @()
    VariablesToExport = @()
    AliasesToExport = @()
    PrivateData = @{ PSData = @{ Tags = @('I2C','Profiles','ESP32','RP2350') } }
}

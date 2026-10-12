# PowerShell Tools

## Import

Copy the entire `I2CMonitor` folder. This isolated module includes powershell-yaml 0.4.12, YamlDotNet and PowerShellYamlSerializer DLLs. No module installation, internet connection or PowerShell 7 is required. Windows PowerShell 5.1 is supported.

```powershell
Import-Module .\I2CMonitor\I2CMonitor.psd1
Get-Command -Module I2CMonitor
Get-Help Publish-Profile -Full
```

Relative paths use the caller's PowerShell location. YAML accepts position 0; COM ports are strictly named (`-Port COM18`). Omitted ports are auto-detected, with a selection prompt if ambiguous. Use the tablet USB data cable and close other serial monitors. Runtime tools do not require the firmware source tree.

## Convert-Profile

Validates YAML and writes an IPRF v1 package beside it, or to `-Output`. Bounds, references, formats and conversions are validated before writing. This checks the schema, not the physical truth of a chip definition.

```powershell
Convert-Profile .\ip2366.yaml
Convert-Profile .\ip2366.yaml -Output .\packages\ip2366.i2cp
```

## Publish-Profile

Converts and uploads YAML in one command. Matching chip IDs always warn and ask for replacement confirmation, even at the same version. `-Force` skips the prompt but never validation. Capture pauses; chunks are acknowledged and stored bytes are verified before reboot. After an uncertain commit, inspect inventory before retrying. This command does not blindly retry writes.

```powershell
Publish-Profile .\ip2366.yaml
Publish-Profile .\ip2366.yaml -Port COM18 -Force
```

## Get-TabletProfiles

Returns objects with Chip, Name, Revision, Source and Crc. Source distinguishes compiled and uploaded definitions. Inventory neither pauses nor modifies capture.

```powershell
Get-TabletProfiles | Format-Table
Get-TabletProfiles -Port COM18 | Export-Csv .\inventory.csv -NoTypeInformation
```

## New-ProfileHeaders

Maintainer tool: regenerates compiled fallback profile, report and experiment headers from a YAML directory. Runtime uploads do not require this tool/compiler. Supply explicit paths when the module is outside the firmware tree.

```powershell
New-ProfileHeaders -ProfileDirectory .\profiles -Output .\include\generated_profiles.h
```

## New-ExperimentHeader

Maintainer tool: regenerates identification, graph and recorder filter descriptors. Also called by New-ProfileHeaders.

```powershell
New-ExperimentHeader -ProfileDirectory .\profiles -Output .\include\generated_experiments.h
```

## New-MonitorFont

Maintainer tool: regenerates the fixed-width doubled-height monitor font from the pinned LVGL 9.2.2 UNSCII 8 source. Unexpected glyph-table layouts are rejected. Normal builds use the committed header and do not need the font source/tool.

```powershell
New-MonitorFont -Source .\lv_font_unscii_8.c -Output .\mono_font.h
```

## Module Contents

Every public command is an advanced function with comment-based help. Schema, binary and serial helpers are private. Bundled vendor files retain their licensing and implementation; upstream packaging/tests are omitted. Both .NET Framework and Core DLL variants remain for the vendor loader. Serial detection currently targets Windows COM ports; Linux/macOS detection is not implemented.

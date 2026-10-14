# PowerShell Tools

## Import

Copy the entire `I2CMonitor` folder. This isolated module includes powershell-yaml 0.4.12, YamlDotNet and PowerShellYamlSerializer DLLs. No module installation, internet connection or PowerShell 7 is required. Windows PowerShell 5.1 is supported.

```powershell
Import-Module .\I2CMonitor\I2CMonitor.psd1
Get-Command -Module I2CMonitor
Get-Help Publish-Profile -Full
```

Relative paths use the caller's PowerShell location. Profile conversion/upload commands accept YAML at position 0; COM ports are strictly named (`-Port COM18`). Omitted ports are auto-detected, with a selection prompt if ambiguous. Profile installation/inventory uses the tablet USB port; Watch-I2C uses the RP instrument USB port. Close other serial monitors. Runtime tools do not require the firmware source tree.

## Watch-I2C

Operates the RP2350 directly from a PC with no tablet, interpreting USB text transactions using YAML. The module bundles the current IP2366 profile and all console dependencies. The same console also supports the standalone RP2040 USB-text sniffer. This is not the structured UART tablet protocol. Reset the RP if an earlier structured monitor switched its USB output into binary mode, and avoid simultaneous tablet/PC control of capture.

```powershell
Watch-I2C
Watch-I2C -Port COM7
Watch-I2C -ValidateProfile
Watch-I2C -Port COM7 -Profile .\ip2366.yaml -View Unknown
Watch-I2C -Port COM7 -RecordPath .\logs\capture.txt -RecordTime 60
Watch-I2C -InputFile .\logs\capture.txt -ShowFailures
Get-Help Watch-I2C -Full
```

Interactive live sessions start paused: press S to start, P to pause and Q to quit. I selects observed unknown registers for investigation; N/A returns to all registers; K selects known registers; U selects unknown registers; O cycles outcome filtering. D requests diagnostics, T reports timing, F shows failure summaries, and G toggles pulse qualification. The console never issues active I2C queries or writes. Inference remarks appear only with -ShowRemark.

Recording with -RecordPath starts capture automatically. Press Q to finish recording; shutdown requests capture standby before closing the port. The progress display includes elapsed time, recorded events, file size and the stop key. Optional -RecordTime and -MaxRecordItems stop at whichever limit is reached first. Existing output files prompt before overwrite; -Force permits unattended replacement. Recorded files are text transaction/diagnostic logs, not raw sampled-edge capture files. Replay uses -InputFile without a COM connection.

The console decoder supports one- and two-register unsigned properties and its supported assembly rules; it is not a replacement for every tablet profile feature. Report, graph, identification and presentation metadata do not change console numeric decoding. Unsupported encoding/assembly is rejected explicitly.

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

# IP2368 Profile Verification

Source: supplied translated original IP2368 register description, **V1.61, 2022-07-13, 21 pages**. SHA256: `A04327A4B396D3B416549AA105CAFB4C8F02143A36D0C015275DF359461577DC`. All pages were reviewed; critical mode, status and ADC tables were also inspected visually.

Scope is **IP2368_I2C_COUT / IP2368_I2C_NACT**, per page 3. Default 7-bit address is 0x75 (wire addresses 0xEA/0xEB, page 2). Each YAML property records its source page. Reserved bits are omitted. This is passive interpretation, not a control/protection specification.

## Important Differences

- 0x04 is capacity in 200 mAh/code, not the IP2369 cell-count field.
- 0x31 bits 2:0 are charging state; the input charging-mode field is in 0x32 bits 7:6.
- 0x33 charging-voltage codes start at 2 for 5 V, unlike IP2369.
- 0x35 bit 6 is the input MOS state, not available-PDO flags.
- System power is **24 bits across 0x74+0x75+0x76, in mW/code**, not two bytes at 10 mW/code. All three consecutive successful reads are required; missing, failed, reordered, expired or gapped components cannot become an OK measurement. The 1 s bound is tablet arrival time, not a physical bus timestamp or proof of atomic ADC sampling.
- Voltage pairs use mV/code; current pairs use mA/code. Current signedness is not defined and is not guessed.
- GPIO/NTC readings remain voltages; no temperature or configured-limit inference is made.

## Explicit Limitations And Manual Discrepancies

- Cell target voltage (0x02) depends on battery chemistry in 0x01 bit 2. Charge limit (0x03) depends on current/power mode in 0x01 bit 0. The present profile keeps these as codes with both formulas in details; it does not invent a mode from defaults. Conditional cross-register decoding is not yet supported.
- Trickle/undervoltage thresholds show both chemistry-qualified per-cell choices. Pack undervoltage also requires the actual series-cell count.
- 0x06 specifies `Cap_Now=N` but does not explicitly define a unit; it remains a code. 0x30 explicitly specifies percent and is decoded accordingly.
- 0x09 bit 5 has a translated shutdown-disable heading but enable/disable descriptions. Its polarity remains unresolved and is displayed as an explicit code choice.
- PPS default text says 5 A while reset 0x3C at 50 mA/code gives 3 A. The formula is used; reset values are not used as live data.
- 0x38 says write-one-to-clear but the table says R. Only read interpretation is enabled.
- Page 17 incorrectly calls 0x31 bit 5 a discharge flag, contradicting its charging-state table. No incorrect cross-register dependency is applied.
- 0x80+0x81 identify GPIO4 but repeat the GPIO3 formula. Common voltage scaling is provisional and marked in the YAML.
- The translated QC validity qualification is unclear; the flags do not establish successful high-voltage fast charging.
- Software known-value tests establish implementation consistency, not independent IP2368 hardware validation.

## Maintenance And Bench Test

Edit `ip2368.yaml`, then run `tools/Build-Profiles.ps1`. `tools/New-IP2368Profile.ps1` is the reproducible import recipe; rerunning it overwrites YAML refinements.

With address 0x75, choose one of IP2366, IP2368 or IP2369 in the existing assignment dialog. An intentionally wrong profile can decode shared fields but will differ for the registers listed above. Pause to persist the choice. Change Profile remains address-wide. RP2350 firmware and UART wiring/protocol are unchanged.

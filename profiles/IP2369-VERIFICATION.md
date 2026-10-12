# IP2369 Profile Verification

Primary source: user-supplied **IP2369_AC Register Description Document V1.10**, dated 2025-07-23, 26 pages. SHA256: `67E3DD9D59E2ABEC1D6DA102224E0BD5FC9564F94423FE7C3C4C2EBD88026308`. All pages were reviewed; key register table images were checked against extracted text. The Markdown reference was used to structure the profile, not to override the original manual.

## Verified Conversion Boundary

| Registers | Interpretation | Manual pages |
| --- | --- | --- |
| 0x50+0x51 | Battery voltage: combined unsigned code in mV | 21 |
| 0x52+0x53 | System voltage: combined unsigned code in mV | 21 |
| 0x54+0x55, 0x56+0x57 | Port MOS current: code in mA, assuming about 10 mOhm MOS resistance | 21-22 |
| 0x6E+0x6F, 0x70+0x71 | Battery/system current: combined code in mA; physical sign unspecified | 23-24 |
| 0x74+0x75 | System power: combined code multiplied by 10 mW | 24 |
| 0x78+0x79 | NTC/GPIO voltage: combined code in mV, 0..3300 mV | 24-25 |
| 0x3A bits 6:0 | Die temperature in degC, independent of thermal-loop flag | 20 |
| 0x77 bit 7 | 0 selects 20 uA; 1 selects 80 uA | 24 |
| 0x02 | Cell target: 2500 + 10*code mV, maximum 4400 mV | 6 |
| 0x03 | Battery current limit: 100*code mA, maximum 9700 mA | 6 |
| 0x06; 0x08 bits 7:4 | Trickle/termination current: 50*code mA | 7 |
| 0x10 bits 2:0 | Thermal-loop threshold: 80 + 5*code degC | 11 |
| 0x24..0x28; 0x29..0x2A | Advertised PDO base current: 20*code / 50*code mA | 14-15 |

Pair assembly is low-byte first, matching the manual's latch guidance on page 17. Conversion happens once in the decoder. Example: `0x3433` becomes **13363 mV**, not `13363 code`.

## Remaining Uncertainties

- Page 3 limits the manual to IP2369_AC; applicability to other variants is not established.
- Current direction/sign is not specified. The decoder preserves unsigned codes rather than inventing a signed convention.
- Port current scaling assumes the documented MOS resistance; accuracy needs physical validation.
- 0x3A bit 7 is named a thermal-loop trigger flag without explicit polarity. Active-high interpretation remains marked as inferred from IP2366.
- 0x0C contains an unspecified reset power code; unmapped values remain UNDEFINED. Its enable polarity is cross-checked against IP2366.
- 0x0D is described as selectable but has R access; write decoding is not enabled.
- 0x38 describes write-one-to-clear while its table says R. This contradiction remains noted, without enabling active writes.
- 0x32 At_Same has an explicit 0/1 mapping but an unexplained common-placement meaning.
- 0x10 has a malformed reset value in the document. No replacement reset value is invented.
- NTC voltage is not temperature without a verified sensor/divider conversion.
- Atomic steps are display/hysteresis policy, not measurement resolution or calibration guarantees.

This is a passive analysis profile. Known-value software tests are not independent IP2369 hardware validation.

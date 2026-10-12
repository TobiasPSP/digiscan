# IP2366 / IP2368 / IP2369 Identification Evidence

## Scope

All three profiles normally use seven-bit address `0x75`; the manuals' `0xEA`
is the address byte for writes, not another seven-bit address. Address discovery
therefore cannot distinguish these chips. No exclusive, guaranteed family ID was
found in the supplied manuals. Results are **advisory evidence**, never an automatic
positive identification or an automatic persisted profile replacement.

## Useful Observations

| Register(s) | Evidence | Limitations |
| --- | --- | --- |
| `0x39` | IP2369 V1.10 p20 defines IC revision: 1=B, 2=C, 3=D, continuing for later revisions. | Undocumented in the supplied IP2366/IP2368 references does not mean absent. A nonzero revision is a clue, not exclusive proof. |
| `0x04` | IP2366/IP2369 direct cell count is 2-6. IP2368 V1.61 p5 describes battery capacity in 200 mAh counts instead. | IP2368 capacity can coincidentally be 2-6. A value above 6 supports the capacity interpretation but does not prove a family. |
| `0x33[2:0]` | IP2368 V1.61 p14 uses 2=5 V, 3=7 V, 4=9 V, 5=12 V, 6=15 V, 7=20 V. IP2369 V1.10 p18 uses 1=5 V through 6=20 V. IP2366 uses the latter sequence, additionally 7=28 V in the reference. | Only meaningful with input powered. Compare against voltage ADC `0x52+0x53`; configuration alone is not measured voltage. |
| `0x74+0x75(+0x76)` | IP2368 V1.61 p18: 24-bit little-endian power, 1 mW/count. IP2369 V1.10 p24: 16-bit little-endian power, 10 mW/count. The IP2366 reference and independent DUT measurements confirm 10 mW/count. | Compare against voltage/current at a steady, meaningful load. Sequential reads are not a simultaneous ADC snapshot; measurement nodes and conversion losses can differ. |
| `0x54+0x55`, `0x56+0x57` | IP2369 documents additional port-current measurements. | Other chips also expose overlapping measurements. Zero, ACK or NACK at these addresses is not an exclusive signature. Not used to claim identity. |

## Revised Implementation (2026-10-10)

The Identify button explicitly reads the union of advisory YAML field rules and
`0x33`, `0x52`, `0x53`, `0x70`, `0x71`, `0x74`, `0x75`, `0x76`.
No identification writes are issued.

- Rules now declare class A (verified exclusive), B (verified mandatory), or C
  (shared/contextual). The converter requires a source for every rule and explicit
  `exclusive_verified: true` / `mandatory_verified: true` for A / B respectively.
  All currently enabled rules are C: no unconditional exclusive ID or mandatory
  signature was established in the references. There are no additive scores.
- A match identifies; a B mismatch excludes. Failed reads are inconclusive in all
  classes. C mismatches never exclude. Conflicting A/B results are reported as a
  conflict. Only a unique uncontested A match can preselect a profile in the dialog.
- Power consistency is C support if `VSYS_mV * ISYS_mA / 1000` is at least 500 mW
  and the candidate power conversion agrees within 30%.
- Powered charging-voltage category consistency is C support within 10% of the
  measured voltage. IP2368 and IP2366/IP2369 use their different enumerations.
- These tolerances are heuristic, not manufacturer guarantees. The voltage check
  requires input-powered bit 7 set and overvoltage bit 6 clear. Undefined codes,
  missing/NACK reads and unmet prerequisites are inconclusive.
  Zero load leaves the power check inconclusive.
- Every acquired byte is displayed. IP2366 and IP2369 can remain indistinguishable.
  A matching IP2369 revision clue does not identify IP2369 or override a profile.
  Confirming the dialog choice with Done updates the shared parser association;
  scan tile labels always read that same association. X leaves it unchanged.
- The comparison checks are currently specific to this IP family. YAML range rules
  are data-driven; the future portable schema must also represent comparison formulas
  before these heuristics can be transported as arbitrary external profiles.

Reading an undocumented register can have undocumented side effects. Identify is
therefore an explicit experimental action, not a background scan. Absence of a
documented register is **not** treated as a required NACK or zero value.

## Useful DUT Follow-Up

### Explanation of the Known IP2366 Scores 1 / 0 / 3

The old IP2366 rule awarded one point when `0x04 & 0x07` was 2-6. IP2369
shared this rule and awarded another two points when 0x39 was nonzero. IP2368's
old rule instead required the entire 0x04 byte to be 7-255. The lower bound 7 was
an artificial heuristic to avoid the cell-count overlap, NOT a datasheet requirement.

IP2368 V1.61 printed p6 defines bit 7 as enable, bits 6:0 as a capacity code in
200 mAh counts. Code 4 is representable and means 800 mAh; it does not contradict
IP2368. The new C rule masks 0x7F and accepts 0-127. Its compatibility is deliberately
non-distinguishing. The prior reference to p5 for this register was incorrect.

The reported 1/0/3 arithmetic indicates a shared cell-count match and a nonzero
revision clue, with no analog points contributing to IP2366. This is a deduction
from the old rules, not a reconstruction of supplied raw bytes. Zero points for
IP2368 meant no heuristic matches, not exclusion. Nonzero 0x39 on the known IP2366
demonstrates why that byte cannot be assumed exclusive to IP2369.

### Candidates for Class A / B

| Candidate | Assessment |
| --- | --- |
| 0x39 revision | Not A: absence from another manual does not establish exclusivity. Not unconditional B: historical/other revision encodings are not established. C only. |
| 0x04 cell-count range | IP2369 p6 documents 2-6. Out-of-range is a configuration contradiction, not a silicon identity: the field is writable and can be invalid. IP2366 mapping is experimentally inferred. Do not use as unconditional B. |
| Powered 0x33 plus known voltage | Promising conditional B once stable powered state, measurement node and variant are verified. Sequential reads and conflicting IP2366 codebooks prevent hard exclusion now. C only. |
| Power 0x74-0x76 compared with V/I | Strong contextual format discriminator, but sequential values are not synchronized and the 30% tolerance is not guaranteed. C only, not hard exclusion. |
| 0x69-0x6D manufacturer text | IP2366 DUT YLSII is not a fixed chip-specific ID. Not A/B. |
| Reset values / reserved bits | Current controller can change defaults; reserved read values are not guaranteed. Not A/B. |

No unconditional A/B rule was verified. Engine tests cover A-positive, B-exclusion,
A/B conflict, C mismatch and failed-read inconclusiveness using synthetic signatures;
the production profiles must not claim those guarantees without primary evidence.

Collect `0x39`, `0x04`, powered `0x33`, voltage/current and power under steady load
on each known chip. Consistent behavior across known samples may justify stronger
signatures later. In particular, do not declare `0x39` exclusive until its behavior
on real IP2366 and IP2368 devices is known.

## Primary References

- IP2368 register manual V1.61, supplied `1049017603-Ip2368-i2c-Cout-With-Reg-v1-61-En.pdf`.
- IP2369 register manual V1.10, supplied `1020831934-IP2369-AC-with-Reg-V1-10-En.pdf`.
- IP2366 supplied V1.00/V1.13 references and the consolidated I2C Reference README,
  including the user's independently checked current and power readings.

Page references are printed manual pages, which match the supplied PDF page indexes
plus one for the IP2368/IP2369 documents. No undocumented electrical behavior is
assumed from register names or shared addresses.

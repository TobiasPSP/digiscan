# V22 validation

## Scope

Tablet V22 and RP2350 V13 are paired revisions. V21/V12 remain available unchanged in behavior. The V21 README received paragraph-only reflow requested by the user, and its edited introduction was copied into V22 without wording changes.

## Acquisition design

- Opcode 22 carries bounded address/register/length read plans. The tablet sends at most 64 plans per packet, within the existing 267-byte command encoder limit.
- The RP retains the same private PIO START/STOP witness used by fast scan. Every byte still waits for bus idle and releases both GPIOs afterward. This is not continuous ownership of the I2C bus.
- Shared bit fields with identical address/register span are deduplicated. Active replies refresh all corresponding cached fields.
- Only the final result confirms capture restoration. Intermediate results never claim restoration. Cancellation, disconnect/lease loss, deadline, bus contention, and restoration faults stop safely.
- Active reads retain the existing 10 kHz controller configuration and one-byte pointer/read procedure. No device auto-increment semantics, electrical settings, or pull-ups were changed.
- GRAPH START explicitly acknowledges filter setup, subscription and capture enable for Passive/Hybrid modes. STOP freezes graph history immediately and cancels graph-owned active work.
- Hybrid freshness uses successful passive observation timestamps in wall-clock milliseconds, separate from first-read highlighting and active replies. Default interval is 1 second; selectable range is 1-30 seconds.
- Clear history removes all chart points only. Selected properties, cached readings, acquisition mode, interval and running state remain unchanged.

## Checks

- PlatformIO tablet and WeAct RP2350B headless builds succeed with pinned dependencies.
- Full native LVGL preview suite passes, including actual UART/COBS/CRC handling, graph mode controls, START from paused capture, STOP freezing, history clear, hybrid freshness, active batches, cancellation, footer bounds and accelerated Read Missing.
- Existing active-read lifecycle, scan-batch and exact-SDK hardware-mock tests pass.
- New native read-batch tests cover one retained witness, per-byte pin release, armed/paused restoration, ACK/NACK, cancellation, backpressure, arbitration loss, busy bus and invalid plans.
- Report and structured protocol regression tests pass under Windows PowerShell 5.1.
- Embedded static stack-frame guard passes. It does not establish live high-water margins for nested library calls.
- Graph footer screenshot was rendered by the real LVGL fixture and visually inspected.

## Single-register completion correction

Live testing exposed false INCOMPLETE results for successful single-byte batch reads. Such profile definitions use high=0 as an unused companion-register field, whereas the RP reports the last requested byte address. Batch validation now checks low+count-1, matching the existing individual-read path and the actual RP Job result. The native UART fixture was corrected to produce actual acquisition addresses rather than copying the profile's high field. It explicitly checks successful single- and multi-register cache entries. This correction requires only a tablet V22 rebuild/upload; RP2350 V13 is unchanged.

## Live checks still required

1. Flash both V22 tablet and V13 RP firmware. Confirm passive monitoring still works.
2. Compare Read Missing/Read All completion time with the previous version, first paused and then with host traffic present. Verify previously collected readings remain retained.
3. Select graph properties, use Passive, STOP, then START. Verify it resumes without visiting START/scanner. Repeat several times.
4. In Hybrid mode, verify regularly polled properties avoid active reads while unpolled properties refresh at the selected interval. In Active mode, verify all selected properties refresh.
5. Clear while running and while stopped. History should empty, controls and selections should remain unchanged.
6. STOP during an active read and disconnect/reconnect the RP. Verify no continued graph updates and normal capture recovery. Shared-bus collision avoidance remains best-effort, not a guarantee against an uncooperative second master.

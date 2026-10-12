# IPRF Binary Format 1

This is a portable byte stream, not a serialized C++ struct or executable code.
All integers are unsigned little-endian. Numeric conversions/ranges for graphs
use little-endian IEEE-754 binary64. Text `T` is `u16 byteLengthIncludingNul`,
followed by UTF-8 bytes and exactly one final NUL; embedded NULs are invalid.
The empty string has length 1. Arrays start with their stated unsigned count.
Booleans use 0 or 1. Limits are checked before allocation or indexing.

## Header

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 4 bytes | ASCII `IPRF` |
| 4 | u16 | Format version, 1 |
| 6 | u16 | Header size, 24 |
| 8 | u32 | Total file length including header |
| 12 | u32 | CRC-32/ISO-HDLC of bytes 24 through EOF |
| 16 | u32 | Required feature flags, zero in v1 |
| 20 | u32 | Reserved, zero |

CRC uses reflected polynomial 0xEDB88320, initial/final XOR 0xFFFFFFFF;
`123456789` produces 0xCBF43926. Unknown versions/features are rejected rather
than interpreted optimistically. No trailing bytes are allowed.

## Body Order

1. Device: `T id`, `T name`, `u16 revision`, `u8 addressCount`, address bytes,
   `u8 readProtocol`, `T source`. Protocol 0 is passive-only; 1 is the verified
   single-byte-pointer/repeated-START/single-byte-read protocol at 10,000 Hz.
2. Properties: `u16 count`, then each record described below.
3. Reports: `u16 count`, each `T header`, `u16 groupCount`; each group is
   `T header` (empty hides heading), `u16 itemCount`, `T reference` per item.
4. Identification: `u16 count`, each `T label`, `u8 register`, `u8 mask`,
   `u8 min`, `u8 max`, `u8 class` (A=0, B=1, C=2). Comparison masks the raw byte
   without shifting, matching the existing evaluator. Class A/B source assertions
   are validated on the PC and retained in metadata, not inferred by the loader.
5. Graphs: `u16 count`, each `T title`, `u8 lineCount`; each line has
   `T reference`, `f64 min`, `f64 max`, `u32 RGB`, `u8 area`.
6. Recorder presets: `u16 count`, each `T name`, `u16 registerCount`, register bytes.
7. `T metadataJson`: provenance, transport, optional hardware and policy metadata,
   including identification source assertions. Stored, not evaluated by the MCU.

## Property Record

Eleven strings in order: id, name, unit, note, bit0 status, bit1 status,
meaning0, meaning1, technical description, friendly description, access.

Then six u8 values: typical address, low register, high register (zero for a
single-byte property), byte count (1..3), shift, display precision (0..6).
Then u32 mask, minimum code, maximum code; f64 scale, offset, atomic step;
u8 flags; u16 enum count; each enum is u32 code and T text.

Flags: bit 0 writable; bit 1 ASCII; bit 2 conflicting interpretation;
bit 3 unsigned-current-code annotation; bit 4 active-readable; bit 5 boolean
metadata present. Bits 6/7 are reserved and rejected. When bit 5 is absent,
empty boolean strings become null pointers at the runtime integration boundary.

Numeric interpretation is `(raw & mask) >> shift`, then
`value_in_declared_unit = code * scale + offset`. Atomic step uses the same
declared output unit. No implicit mV/V or mA/A conversion is applied elsewhere.
The assigned bus address replaces the property's typical address at runtime.
Finite conversions, contiguous masks, valid ranges, unique IDs/nonoverlapping
fields, enums, UTF-8 and bounded counts are validated.

## Storage Wrapper

LittleFS filenames are `/<device.id>.a` and `.b`. Each stored file starts with
12 bytes: `PRFS`, u32 install generation, u32 CRC32 of the preceding eight bytes.
The complete IPRF package follows unchanged. This wrapper is not uploaded.
Generations order installations independently of chip revisions. The inactive
slot is replaced; boot selects the newest valid slot. Compiled profiles remain
the recovery source when no valid installed slot exists.

## USB Management Protocol

USB CDC is separate from the RP UART. ASCII lines end with LF; CR is ignored.
Requests and replies start `PRF1 <8-hex-session-token> `. Replies carrying another
token and unrelated firmware log lines are ignored by the PC. Maximum request
line length is 1199 bytes; DATA carries at most 512 bytes encoded as hex.

Commands:

```text
HELLO
BEGIN <device.id> <totalLengthDecimal> <bodyCrc8Hex> <replaceConfirmed0or1>
DATA <offsetDecimal> <hexBytes>
COMMIT
ABORT
```

HELLO replies: `TABLET V20 IPRF1 <storageState>`, one
`CHIP <id> <revision> <CRC8Hex> <builtin|uploaded> <name>` per chip, then `END`.
BEGIN replies `READY` only after capture is paused, or `ERROR <reason>`.
DATA replies `ACK <nextOffset>`. COMMIT replies
`COMMITTED <id> <revision> <CRC8Hex>` only after verified flash storage, then
reboots after 500 ms. ABORT replies `ABORTED`. Inactivity aborts after 20 seconds.
Only the owner token can send DATA/COMMIT/ABORT. Replacement confirmation is
required even when the numeric revision and binary content are unchanged.

## Verified Dependencies

Encoder scalar widths use .NET Framework BinaryWriter overloads; custom string
framing deliberately does not use BinaryWriter.Write(string).
[Microsoft BinaryWriter documentation](https://learn.microsoft.com/en-us/dotnet/api/system.io.binarywriter.write?view=netframework-4.8.1)
and [SerialPort.ReadExisting](https://learn.microsoft.com/en-us/dotnet/api/system.io.ports.serialport.readexisting?view=netframework-4.8.1)
were checked; persistent partial lines are handled across calls.

Filesystem APIs were checked against the installed Arduino ESP32 2.0.17
`libraries/LittleFS/src/LittleFS.h` and `libraries/FS/src/FS.h`: begin's default
partition label is `spiffs`, formatting is opt-in, writes report byte counts,
flush is void, and close is followed by explicit read-back validation.

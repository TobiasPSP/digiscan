# RP2350 Sniffer V13

PlatformIO firmware for the headless WeAct RP2350B I2C instrument, paired with Tablet V22. It supports passive capture, 256-byte payloads, timing diagnostics, pulse qualification, USB serial output, structured UART delivery, and explicitly requested active scans, reads, writes and recorder playback. Ordinary monitoring is passive. Shared-bus active operations use idle admission and interference detection, but cannot guarantee collision-free operation with another controller.

## Build

Open this folder in VS Code with PlatformIO. Select `weact_rp2350b_headless`, or run:

```powershell
pio run -e weact_rp2350b_headless
```

The compiled firmware is `.pio/build/weact_rp2350b_headless/firmware.uf2`. Hold BOOT while resetting or connecting the RP board, then copy that UF2 onto its bootloader drive. Use a USB data cable and disconnect DUT probes while changing wiring or installing firmware.

Use the WeAct RP2350B board with A4 silicon (chip marking RP2350B0A4). The local board definition is in `boards`. PlatformIO installs the Arduino core and toolchain configured in `platformio.ini`. The post-build script checks linker RAM headroom and authored stack-frame sizes; do not bypass failed checks.

## Wiring

| Board connection | Destination | Purpose |
| --- | --- | --- |
| GP30 | DUT SDA | I2C probe and explicit active operations |
| GP31 | DUT SCL | I2C probe and explicit active operations |
| GND | DUT GND | Signal reference |
| GP0 TX | Tablet GPIO17 RX | Structured UART output |
| GP1 RX | Tablet GPIO18 TX | Tablet commands |
| GND | Tablet GND | UART reference |

Use 3.3 V signals. Do not add pull-ups to an already operational DUT bus. Power the tablet and RP separately; do not join their power rails. UART starts at 115200 baud for rendezvous; the tablet negotiates higher speeds. The onboard LED blinks slowly when paused and faster when capturing.

## RP2040 Build Profile

An alternative `rp2040_zero` environment remains in `platformio.ini`. It uses SDA GP4, SCL GP5 and the original external TFT/RGB LED setup, not the headless RP2350 wiring. Build with `pio run -e rp2040_zero`; output is `.pio/build/rp2040_zero/firmware.uf2`. This latest environment has not been revalidated on RP2040 hardware. Never use the RP2350 binary on an RP2040.

## Project Layout

- `src`: firmware source, including the PIO program and its generated header. `decoder_tests.h` is included by the firmware and is retained.
- `boards`: WeAct board definition.
- `scripts`: required post-build memory checks.
- `platformio.ini`: environments, dependencies and GPIO configuration.
- `.pio`: generated PlatformIO dependencies and build output, not source.

Historical protocol notes, regression fixtures, analysis recordings and PC tools are stored outside this build project in `../publication-maintenance/rp2350-v13`. They are not needed to compile the firmware.

# Changes

## 2026-10-12 — Speaker-reactive LED candidate 999.0.6

Added gated `led.reactive` modes on the existing PCM tap and LED worker,
including show ownership and MCP validation. Build and host verification pass;
physical CPU/audio/LED timing awaits operator OTA validation. See
[reactive mode](esp32/devices/herald-reactive.md). No hardware deployment occurred.

## 2026-10-12 — Record the hardware and verification history

Added a [current-state guide](docs/herald-current-state.md) and a detailed
[narrative](docs/migrations/2026-10-herald-usb-to-speech.md) linking the original
USB investigation to Herald's speaker, external LEDs and remote control.
The record captures why device identity mattered, why wired I2S replaced the
Bluetooth-speaker idea, how the original firmware was backed up, and which
later results are physical tests versus source or host-test evidence.

The historical baseline was version 999.0.0 with an 8 MB image header on a
16 MB flash chip. Later local BLE records describe 999.0.3, 7,093-byte Link
registration, 235 successful firmware tests with one crypto skip, nine MCP
tests and a 16-peer scan with no measured Wi-Fi reconnects. Those measurements
were reconciled from existing records, not rerun for this documentation change.
The BLE implementation remains outside this documentation commit.

Personal device identifiers and private artifact locations are excluded from
these publishable versions. Existing implementation changes were left unstaged.
No firmware, wiring, pairing or provisioning was changed.

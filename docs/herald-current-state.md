# Herald project state

Recorded 2026-10-12. Herald (Hal) extends Meta's Muse Gadget SDK on an HW678
ESP32-S3 N16R8: 16 MB quad flash, 8 MB octal PSRAM, an NS4168 I2S amplifier
and two external common-cathode RGB LEDs.

The canonical profile is `hw678-n16r8-herald`. The old `xiao-s3` alias is
compatibility naming, not a Seeed XIAO hardware identification. Read the
[wiring guide](../esp32/devices/herald-wiring.md) before assembling hardware,
then [Build Herald](../esp32/devices/herald.md). Exact USB device identifiers
and generated serial port names are installation-specific: identify your own
board before flashing.

The original headless Muse build already authenticated, registered commands
and opened its home-network tunnel. Later firmware adds tested speaker output,
PCM WAV playback, external speech synthesis, two-LED patterns, persistent
settings, diagnostics and an optional authenticated HTTPS LAN endpoint.
[Remote management](../esp32/devices/herald-remote-management.md) and the
[MCP/CLI guide](../herald-mcp/README.md) describe those interfaces.

An existing local validation record describes firmware 999.0.3 BLE scanning:
16 peers, 13 successful concurrent Wi-Fi status checks, zero reconnects and
preserved volume/brightness settings. Its associated implementation is still
being developed in the local worktree. This documentation pass did not flash
firmware, repeat tests, publish those code edits or prove live GATT operations.
Read the [narrative record](migrations/2026-10-herald-usb-to-speech.md) for the
sequence and evidence limits.

No microphone, display, Bluetooth Classic audio or verified onboard LED pin
is provided by this build. The BLE development is GATT control, not A2DP audio.
End-to-end OTA delivery remains unverified in the latest local update record.

Use ESP-IDF v6.0.1. Supply your own Muse credentials and reachable text-to-WAV
service through a private overlay. Preserve NVS and partition geometry across
updates. Never publish personalized binaries, flash backups, tokens, private
configuration, TLS private keys or raw device logs. Publication checks and
limitations are recorded in [HERALD_RELEASE.md](../HERALD_RELEASE.md).

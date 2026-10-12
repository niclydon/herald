# Herald BLE central (999.0.3)

Herald can act as a BLE GATT client while keeping its Muse Link connection and
phone-provisioning service. Enable `CONFIG_HOMEHUB_BLE_CENTRAL`; the HW678
profile enables it, with two NimBLE connection slots (one central peripheral
plus the existing phone service). There is only one outbound central session.
No Bluetooth Classic, audio streaming, notification subscription or peripheral
bonding/passkey workflow is added. Access requiring peripheral authentication
returns the underlying ATT error.

## Commands

All six commands validate input before starting a dedicated worker and return
`{"_async":true}` to the dispatcher. Their final Link result uses the existing
`{"ok":true,"payload":...}` envelope; error results contain `error.code` and
`error.message`. LAN/MCP callers poll the returned job ID. A second BLE operation
returns `busy`; a second connection returns an instruction to disconnect first.

| Command | Parameters | Final payload |
|---|---|---|
| `ble.scan` | Optional `duration_ms`: integer 1–60000, default 10000 | Array of name, address, RSSI, manufacturer hex and service UUIDs |
| `ble.connect` | `address`: `AA:BB:CC:DD:EE:FF`; optional `timeout_ms`: 100–60000, default 10000 | `{handle}` |
| `ble.discover` | `handle`: integer 0–65534 | Array of services and characteristic UUID/property lists |
| `ble.read` | `handle`, `characteristic_uuid` | `{value_hex}` |
| `ble.write` | `handle`, `characteristic_uuid`, `value_hex`; optional boolean `with_response`, default false | No additional payload |
| `ble.disconnect` | `handle` | No additional payload |

UUIDs accept 4 or 8 hexadecimal digits, or canonical 128-bit form with hyphens.
Values accept even-length hex up to 512 bytes, including an empty write. Unknown
and duplicate parameters are rejected. Duplicate characteristic UUIDs are
reported as ambiguous rather than selecting an arbitrary attribute.

Scan first before connecting: BLE MAC addresses alone do not encode whether the
address is public or random. The latest scan retains address types; an address
absent from that scan is tried as public. Random private addresses can rotate,
so rescan when reconnecting. Legacy advertising and scan responses are merged
by address/type. Results contain up to 16 observed peers, sorted by RSSI; these
are not necessarily the strongest 16 in a crowded environment. Names are
bounded to 31 printable ASCII characters (other bytes become `?`), manufacturer
data to 31 bytes, and advertised UUIDs to eight per peer. Extended advertising
is not enabled.

Discovery is bounded to 16 services and 64 characteristics and fails explicitly
when those limits are exceeded. Read/write refresh discovery before resolving
the UUID. Attribute reads support long values up to 512 bytes. A timed-out GATT
operation requests disconnection so its unfinished procedure cannot be reused.
Callbacks carry generation identifiers rather than references to freed jobs.

## Write semantics

Writes are sequential and each chunk is at most the negotiated ATT MTU minus
three bytes. With responses enabled, each chunk waits for its ATT response;
without responses, chunks are paced at 20 ms and success means submitted to the
stack, not acknowledged by the peripheral. Errors identify the zero-based chunk
and byte offset; earlier chunks may already have changed the peripheral.

These are **separate writes to the same characteristic**, not an atomic ATT
Prepare/Execute long write. Use a peripheral protocol that explicitly accepts
chunked messages. For a normal register-like characteristic, send a value that
fits in one chunk; otherwise later writes may replace earlier ones. Never retry
a failed write automatically without understanding the peripheral's state.

## Wi-Fi coexistence and provisioning

Active scans use a 100 ms interval and 15 ms window. ESP-IDF software coexistence
is enabled. Bluetooth's host and controller run on core 1; Wi-Fi remains on core
0. NimBLE stays initialized after setup, but setup advertising still stops.
Central GAP events use their own callback and do not pass through the phone
pairing connection handler.

These choices follow the [ESP-IDF coexistence guidance](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-guides/coexist.html)
and use the [existing NimBLE host](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/bluetooth/nimble/index.html).
They reduce contention; live coexistence must still be checked on the installed
firmware. Observe the `link.ble.central` start/completion logs, verify Link remains
connected, and compare `wifi.status` reconnect count and device uptime before
and after scanning. Host tests alone cannot establish RF coexistence.

## Updating and checking

Build with ESP-IDF v6.0.1, the existing private overlay and version 999.0.3 (or a
newer version). Regenerate the selected build directory's sdkconfig when changing
board overlays. Both `tools/board.sh hw678-n16r8-herald build` and the deprecated
`tools/board.sh xiao-s3 build` select this hardware profile, but use separate
build directories.

Deliver the signed app through the existing Muse `device.ota` command using a
private, trusted HTTPS URL. The installed 999.0.2 prototype has LAN admin disabled;
adding `admin:true` to the client does not enable firmware OTA on that path.
Keep personalized binaries private. No USB flash or NVS erase is needed.

After reboot, refresh Muse's device description and run `ble.scan` with
`{"duration_ms":10000}`. Confirm Link remains usable, then connect only to a
peripheral you intend to operate. Hardware scans and GATT writes are not run by
the host test suite. The MCP package exposes matching `herald_ble_*` tools;
restart its process after updating the package.

## Prototype verification — 2026-10-12

Firmware 999.0.3 was installed on the identified HW678 prototype over USB after
the owner explicitly authorized an exception to the initial OTA-only request.
Existing pairing and saved settings were preserved. The signed OTA app remains
available privately; it is not included in the repository.

- ESP-IDF v6.0.1 build through `tools/board.sh xiao-s3 build`: **0x161000 bytes**
  in a **0x200000-byte** slot, **31% free**.
- **235 firmware host tests passed**, one host PSA Crypto test skipped;
  **9 MCP tests passed**. Full Link registration: **7,093 bytes**.
- Boot reached `session established`, `link.register acked` and OTA validation.
  No panic was observed during boot or scanning.
- A ten-second active scan returned 16 peers in RSSI order. Thirteen concurrent
  Wi-Fi status checks succeeded, reconnect count stayed at zero, and uptime
  increased from 15 to 26 seconds. Serial logs showed Link connected at scan
  start and completion, with no intervening Link re-registration.
- Saved volume 75 and brightness 100 were retained. Live GATT connect/read/write
  against a selected peripheral remains unverified; host tests cover discovery,
  reads, chunking, write failure, timeout cleanup and the single-session guard.

The attempted OTA handoff was not used: the installed LAN endpoint had admin
commands disabled and a harmless test WAV could not be fetched from the candidate
HTTPS host. No personalized firmware was uploaded there; the probe file was
removed. This update therefore does not claim end-to-end OTA delivery validation.

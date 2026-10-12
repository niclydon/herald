# From USB identity to Herald

Recorded on 2026-10-12. The initial investigation took place on the Apple Silicon
MacBook on 2026-10-11. This narrative combines that conversation's direct tool
evidence with later documentation and validation artifacts already present in
the workspace. The later work is attributed to those records rather than
represented as actions performed during this documentation update.

## A connector was not a board identity

The investigation began with a deceptively simple request: what was connected
over USB? macOS initially reported `USB Single Serial`, VID/PID `1a86:55d3`,
bridge serial `[USB serial omitted]`. That established a serial interface, not the exact
PCB. Another attached board later had bridge serial `[USB serial omitted]`. Repeated
checks therefore had to distinguish both recabling and entirely different
devices, rather than assuming every new USB port belonged to the same gadget.

An esptool query of the spare identified an ESP32-S3 revision 0.2 with 8 MB
PSRAM and MAC `[device identifier omitted]`. The user corrected an important assumption:
this was a separate board identical to the one used for Muse. The agent had
conflated them. Subsequent work anchored identity on hardware serial/MAC rather
than conversational labels or changing `/dev/cu.usbmodem*` names.

Switching connectors on the spare changed its descriptor to `Espressif Device`,
`303a:4001`, with firmware-provided serial `[USB serial omitted]`. The Muse board's native USB
instead reported `USB JTAG/serial debug unit`, `303a:1001`, serial/MAC
`[device identifier omitted]`. A later inventory also showed a second native USB device,
`[device identifier omitted]`. This did not prove that second device ran Muse firmware.

## Research narrowed the alternatives, but did not add hardware

The initial project research favored ESPectre for the spare board: local Wi-Fi
CSI motion sensing could use a supported S3 without buying a separate sensor.
TinyGS required LoRa radio hardware, XiaoZhi required audio hardware and a
backend, and Bruce's broader RF features depended on compatible peripherals.
These were recommendations, not flashed installations.

The speaker question exposed another distinction. The S3 supports Bluetooth LE,
but not Bluetooth Classic A2DP for ordinary Bluetooth speaker streaming. A wired
I2S amplifier was the practical route. A MAX98357A was initially recommended as
a purchase option; later photos established that the user actually had an
NS4168. The tested wiring must follow that actual module, not the hypothetical
shopping recommendation. USB host audio was also considered, but the native
connector alone did not establish host-power delivery or audio driver support.

## Establish the baseline before replacing firmware

The official `facebookincubator/muse-gadget-sdk` checkout began at `812c46f`.
Its ESP32 AGENTS file required ESP-IDF v6.0.1 and described how to identify
running firmware. The first `tools/muse/chat.py --status` attempt on native USB
stalled in serial flush. The board was subsequently backed up through that
port, then its running logs were obtained through the other connector's UART
bridge. This resolved the console question without guessing a board profile.

The full original flash read returned 16,777,216 bytes in 73.6 seconds at a
reported 1,824.4 kbit/s. Its SHA256 is
a checksum retained with the private backup.
The backup and extracted application were restricted to mode 0600. They contain
private provisioning material and are not publication artifacts.

The installed application identified itself as `muse-gadget`, version 999.0.0,
built Oct 11 2026 at 19:14:07 with ESP-IDF v6.0.1. The active partition table was
at 0x10000, with two 2 MB OTA slots. An older table was also found at 0x8000;
the boot log identified which table was actually used. Physical flash was
16 MB although the image header declared 8 MB. Octal PSRAM was 8 MB, BOOT used
GPIO0, UART console used GPIO43/44, and status LEDs were disabled.

After transient Wi-Fi retries, the board joined its saved network. Token refresh
returned HTTP 200, its encrypted Muse session was established, `link.register`
was acknowledged, the NAPT tunnel interface came up, and the tunnel stream
returned HTTP 200. Heartbeat showed Wi-Fi, control and tunnel up. Discovery and
OTA strings were present in the application, but those commands were not
invoked during this baseline audit. Successful registration is not a test of
every advertised remote command.

The decision at that point was to avoid replacing working firmware merely to
enable features it already had. The SDK's community skills describe network
device integrations; they are not a collection of hardware drivers that can
all be enabled on a bare S3. The user confirmed a bare board and the N16R8
marking. That marking confirmed memory configuration, not the PCB's LED pin.

## The later hardware record: speech and two lights

The existing capability audit and speaker guide now identify the PCB from user
photos as HW678 V0.0.0. The amplifier labels established this connection:

| NS4168 label | Board connection |
|---|---|
| G | GND |
| V | 3V3 |
| BCL | GPIO4 |
| LRC | GPIO5 |
| DIN | GPIO6 |

The speaker remained on the amplifier output connector. Neither speaker wire
was connected to board ground. The recorded first speaker build was 1,380,352
bytes, with 34% of the 2 MB slot free; 227 host tests passed with one crypto
dependency skip. Boot reported the NS4168 ready and a completed test tone.
The user confirmed hearing it, which is stronger evidence than an I2S write
returning success alone.

The extension added `audio.test`, PCM16 WAV playback and `audio.speak` through
an external Furnace text-to-WAV service. That service, not an LLM on the ESP32,
generates speech. Normal volume settled at 75 after an earlier explicit value
of 35 caused confusion. The source guide specifies 8–48 kHz mono/stereo PCM16
WAV, at most 2 MiB, and excludes compressed MP3/AAC/WAV playback on this path.

Two external common-cathode RGB LEDs were then physically tested with individual
330-ohm series resistors. Their RGB mappings are 17/16/15 and 9/7/8. `demo.show`
prepares six narration clips, waits 20 seconds for recording, and combines
color effects, audio-reactive brightness and a synthesized melody. The demo's
default volume is 100; normal speech remains 75. The onboard LED is still
unidentified and disabled. No microphone input was implemented.

## Herald grows a remote control surface

Commit `730828b`, dated 2026-10-12, records the Herald hardware profile, remote
controls, MCP bridge and build guides. The canonical profile is
`hw678-n16r8-herald`; the retained `xiao-s3` alias is historical compatibility,
not a Seeed hardware identification. The current guides describe LED patterns,
audio cancellation, Wi-Fi status/reconnect, device diagnostics, persistent
volume/brightness settings and an optional authenticated HTTPS LAN transport.

The publication record describes 999.0.2 validation, including 233 firmware
tests without failures and one skip, eight MCP tests and a 5,324-byte full Link
registration. It distinguishes a physically heard speech test from a pattern
completion test that was not visually observed. This precision matters: a
successful remote job result is not always physical evidence.

The next local record covers firmware 999.0.3 and six BLE central commands:
scan, connect, discover, read, write and disconnect. It reports 235 successful
firmware tests with one crypto skip, nine MCP tests, a 0x161000-byte application
in a 0x200000-byte slot with 31% free, and 7,093-byte Link registration beneath
the 8,192-byte limit. BLE remains GATT control, not Bluetooth speaker audio.

`herald-ble-validation.json` independently preserves the scan measurements:
16 peers, 13 successful Wi-Fi status checks, zero reconnects before and after,
uptime increasing from 15 to 26 seconds, saved volume 75 and brightness 100.
The BLE guide records successful boot registration and no panic during the
scan. Live GATT connect/read/write remains unverified against a chosen device.

The same guide explains why an initial OTA-only plan did not deliver the
update: LAN admin was disabled and a harmless WAV probe could not be fetched
from the proposed HTTPS host. No personalized image was uploaded there. The
later record says the owner explicitly authorized USB installation instead.
That outcome must not be described as an end-to-end OTA validation.

## Recovery, publication and remaining work

The original backup remains the recovery baseline, but normal boot subsequently
refreshed tokens and calibration data; restoring it also restores that older
state. Later builds preserve the partition geometry and NVS, and their records
state that no eFuses were burned. Personalized firmware, SDK tokens, TLS keys,
private configuration and raw logs remain outside publication scope.

The public-source record points to `niclydon/herald` and a secret-audit process.
This update did not re-run that audit or verify remote publication. Likewise,
the SDK worktree currently contains active remote-management and BLE edits;
this documentation pass did not stage, revert, commit or modify those edits.

Open work is bounded and explicit: establish the onboard LED pin if desired,
choose actual microphone/display/sensor hardware before adding their drivers,
validate GATT operations with an intended peripheral, and prove an OTA delivery
path before relying on it. The original speaker-connection question is resolved
in the later hardware record, so it must not remain listed as the current blocker.

## Evidence index

- [Herald setup](../../esp32/devices/herald.md).
- [Speaker base profile](../../esp32/devices/n16r8-speaker.md).
- [Remote command reference](../../esp32/devices/herald-remote-management.md).
- [Publication record](../../HERALD_RELEASE.md).
- Original upstream: https://github.com/facebookincubator/muse-gadget-sdk.

The full-flash backup, device identities, personal configuration and later BLE
validation artifacts are retained privately. BLE measurements above describe
local development evidence; the documentation commit does not publish or claim
completion of the accompanying uncommitted implementation.

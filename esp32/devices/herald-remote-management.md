# Herald (Hal): remote management

Herald is a DIY HW678 N16R8 ESP32-S3 with two external RGB LEDs and an
NS4168 amplifier. Start with the [setup guide](herald.md) and
[wiring guide](herald-wiring.md). Pairing identity is preserved across updates.
The canonical profile is `hw678-n16r8-herald`; the deprecated `xiao-s3` helper
alias uses identical settings for old scripts. Neither is a Seeed XIAO pin map.

Friendly BLE/registration name: `CONFIG_HOMEHUB_FRIENDLY_NAME="Herald"`.
Compact name: `CONFIG_HOMEHUB_SHORT_NAME="Hal"`. Pairing keys are retained.

| Command | Parameters | Completion |
|---|---|---|
| `led.set` | Required index 0/1 and r/g/b 0-255; optional brightness 0-100 (100) | Immediate; supersedes pattern |
| `led.pattern` | Required pattern chase/alternate/fade/sparkle and 1-16 RGB triplets; optional speed_ms 20-60000 (300), duration_ms 0-86400000 (0), brightness 0-100 (100) | Async; exactly one result on expiry/supersession |
| `led.clear` | None | Immediate; supersedes pattern and clears both |
| `audio.stop` | None | Requests cancellation of playback; pending synthesis cannot play |
| `wifi.scan` | None | Async; up to 32 unique visible SSIDs with RSSI/channel/numeric auth mode |
| `wifi.status` | None | SSID/IP/RSSI/reconnects/uptime and names |
| `wifi.reconnect` | None | Async rejoin of stored primary network, with 30-second join timeout |
| `device.reboot` | None | Immediate acknowledgement; restart after 500 ms |
| `device.info` | None | Firmware/IDF/chip/flash/PSRAM/heap/uptime and names |
| `device.diagnostics` | None | Async reset reason, minimum heap, OTA state/partition and filtered warning/error tail |
| `settings.get` | None | Cached persistent defaults: volume and brightness |
| `settings.set` | At least one of volume/brightness, integers 0-100 | Async atomic NVS record; cache updates after commit |

Initial persistent defaults are volume 75 and master brightness 100. Explicit
volume overrides the saved default; demo.show still defaults to 100. Saved
brightness scales LED rendering. Static colors use the new default on their
next led.set; running patterns update on their next frame.

One dedicated FreeRTOS worker owns LED pattern jobs. A replacement/clear/set
retires the prior job before installing its successor. Retired jobs send one
session-bound completion and release their copied ID/parameters. A bounded
retirement queue returns busy rather than losing results under command bursts.
Only one worker task exists, regardless of how many patterns are replaced.
The handler takes control/hardware locks with zero wait. If contended, it returns
busy for retry. No pattern allocates a new long-lived task. The host harness
exercises 200 jobs with AddressSanitizer and checks completion uniqueness.

Remote LED commands override demo LED animation; starting demo.show cancels a
remote pattern and claims the LEDs. Existing audio.play/audio.speak/audio.test,
demo.show, device.health/device.discover/device.ota are retained.

## Link and reconnect limitation

Link's handler consumes `_async` as a deferred-result marker, not a separately
transmitted acknowledgement. wifi.reconnect returns ok plus `_async` internally;
its task attempts exactly one final result after joining. Dropping Wi-Fi may
replace the Link session, in which case the transport drops that stale result.
Use wifi.status on the new session to verify reconnection. Never deliver a
previous request's result into an unrelated session. reconnects counts joins
that obtained IP after the initial join, excluding DHCP renewals without a drop.

Diagnostics run outside the command handler. The warning/error tail is bounded
at 2 KiB and omits lines containing credential markers; complete raw logs and
NVS credentials are not returned. Reset reason and OTA state are ESP-IDF enums.
Audio stop cancels actual playback promptly; an in-flight HTTP/TTS operation may
still finish before releasing the speaker worker, without playing its result.

## Build and OTA

Activate ESP-IDF v6.0.1 and set BOARD_PRIVATE_CONFIG to a private overlay containing
the existing SDK token/TTS configuration. Do not commit that overlay or binaries.

```
BOARD_PRIVATE_CONFIG=/absolute/private/sdkconfig.private tools/board.sh hw678-n16r8-herald build
```

Version defaults to 999.0.1 for this profile; set BOARD_FIRMWARE_VERSION to a
higher version for a subsequent OTA. After changing overlays, remove the generated
build-hw678-n16r8-herald/sdkconfig and rebuild. Firmware:
`build-hw678-n16r8-herald/muse-gadget.bin` (application image only, suitable for device.ota).
Partition geometry and NVS remain unchanged. OTA rollback is retained: an OTA
image must regain Link within the existing validation window or roll back.
Secure boot, flash encryption, and pairing eFuse authentication remain disabled.

After installation, refresh Muse's device.describe view and check the commands.
Try device.info, wifi.status, settings.get, a short led.pattern and led.clear.
Then settings.set followed by device.reboot/settings.get verifies persistence.
The platform's device.describe is a remote inspection step, not a new firmware
command in this checkout.

## Optional LAN transport

[Herald MCP and CLI](../../herald-mcp/README.md) reuse these handlers over
authenticated HTTPS, with independent job results, bounded retention and LAN
ownership leases. Firmware LAN control and its administrative subset are
separately opt-in Kconfig options. Muse commands remain available.

## BLE central

Firmware 999.0.3 adds the six asynchronous `ble.*` commands. See the
[BLE guide](herald-ble.md) for parameters, address types, write semantics and
coexistence checks.

See [synchronized shows](herald-show.md) for show.play and show.stop,
which coordinate the existing audio and LED engines on a shared timeline.

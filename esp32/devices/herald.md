# Build Herald

Herald (Hal for short) is a DIY Muse gadget: two RGB LEDs, an I2S speaker,
network-generated speech, persistent settings, and remote management. It runs
on an HW678 ESP32-S3 N16R8. The [wiring guide](herald-wiring.md) is the hardware
contract; the [command reference](herald-remote-management.md) lists parameters.

This is a community addition to the Meta Muse Gadget SDK. You need your own
Muse account/app and SDK token. Muse's service and pairing availability are
external dependencies; this repository does not provide a replacement backend.

## 1. Assemble and install the toolchain

Follow the wiring guide with USB disconnected. Install **ESP-IDF v6.0.1** using
[Espressif's instructions](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/get-started/index.html),
including the ESP32-S3 tools, and activate that installation's `export.sh`.
Use this checkout's `esp32/` directory for all commands below.

The canonical profile is `hw678-n16r8-herald`. It loads `sdkconfig.defaults`,
`sdkconfig.n16r8-speaker`, then `sdkconfig.hw678-n16r8-herald`. The old `xiao-s3`
helper alias remains compatible, but does not describe the physical board.
The feature flag `HOMEHUB_EXTERNAL_RGB_TEST` retains its historical name and
now enables the tested external LED driver and showcase.

## 2. Configure privately

Copy `devices/examples/herald.private.example` to a location outside the Git
checkout, restrict it to your account (`chmod 600`), and edit it locally.
Set `CONFIG_GADGET_SDK_TOKEN` to your own token from the Muse SDK account page.
Do not paste tokens into issue reports or commit generated configurations.

```sh
export BOARD_PRIVATE_CONFIG=/absolute/path/to/sdkconfig.private
# Activate your own ESP-IDF v6.0.1 installation first.
tools/board.sh hw678-n16r8-herald build
```

The private overlay is loaded last. The generated configuration is
`build-hw678-n16r8-herald/sdkconfig`; it overrides later overlay changes.
After changing an overlay, remove that generated file and rebuild:

```sh
rm -f build-hw678-n16r8-herald/sdkconfig
tools/board.sh hw678-n16r8-herald build
```

Output images and generated configuration contain your embedded token. Keep
both private. This fork distributes source, not personalized firmware binaries.
Names can be changed with `CONFIG_HOMEHUB_FRIENDLY_NAME`,
`CONFIG_HOMEHUB_SHORT_NAME` and `CONFIG_GADGET_PRODUCT_NAME` in your private overlay.
Changing a name does not change pairing identity.

## 3. First flash and pairing

Identify the COM port, and verify the chip before flashing:

```sh
python -m serial.tools.list_ports -v
python -m esptool --port YOUR_PORT read-mac
tools/board.sh hw678-n16r8-herald flash YOUR_PORT
python tools/muse/monitor.py YOUR_PORT 20
```

On an already-used board, first make a private full-flash backup with esptool's
`read-flash` command (16 MB for the tested board). Never publish the backup:
it can contain Wi-Fi and pairing credentials. This profile keeps the SDK's
partition geometry. Do not erase NVS when upgrading an existing paired gadget.

A healthy boot reports Herald starting, two RGB LEDs ready, NS4168 ready, and
8 MB PSRAM. Follow the SDK's [pairing instructions](../README.md): enable the
Muse app's developer mode, find **Herald** in Devices, provision Wi-Fi, and
press BOOT when pairing asks for physical confirmation. Already-paired Herald
keeps its identity and stored network. Do not hold BOOT for five seconds:
that is the upstream reset/unpair gesture.

After joining, look for `session established`, `link.register acked`, and
`OTA image validated`. Refresh Muse's device description after firmware updates.
The lack of a built-in display, mic or enabled onboard LED does not imply that
the external LED/speaker commands are absent.

## 4. Speech service

LED control, `audio.test` and `audio.play` work without TTS. For `audio.speak`
and the narrated `demo.show`, supply your own reachable text-to-WAV endpoint:

```text
CONFIG_HOMEHUB_SPEAKER_TTS_URL="https://YOUR_TTS_HOST/v1/tts.wav"
CONFIG_HOMEHUB_SPEAKER_TTS_TOKEN="YOUR_OPTIONAL_BEARER_TOKEN"
```

These values belong only in your private overlay. The endpoint must accept
`POST` JSON `{"text":"Hello from Herald"}` with `Content-Type: application/json`.
Firmware sends an Authorization Bearer header using the configured token,
including an empty token if none is configured. Return HTTP 200 and a PCM16
WAV body: 8–48 kHz, mono or stereo, at most 2 MiB. Redirects and compressed
audio (MP3/AAC/compressed WAV) are unsupported. TTS requests allow up to
240 seconds. HTTPS uses the ESP-IDF certificate bundle; private/self-signed
certificates need a trust configuration change. HTTP is supported, but carries
text and any bearer credential in cleartext on that network.

The original prototype used a separate homelab TTS service. That server and its
model are not bundled here, and this is not an OpenAI audio API client. Adapt
any chosen TTS engine to the request/response contract above. Start by testing
`audio.play` against a small compatible WAV if speech synthesis is unavailable.

## 5. Verify from Muse

Ask Muse to refresh `device.describe` for Herald, then invoke the advertised
commands. These are gadget commands delivered through Muse's Link tools,
not shell commands and not a direct unauthenticated HTTP API on the ESP32.

1. Read `device.info`, `wifi.status`, and `settings.get`.
2. Run `audio.test` with `{"volume":10}`.
3. Test each LED using `led.set`, for example
   `{"index":0,"r":255,"g":0,"b":0,"brightness":50}`.
4. Run `led.pattern` with
   `{"pattern":"fade","colors":[[255,0,0],[0,0,255]],"speed_ms":500,"duration_ms":4000,"brightness":75}`.
5. Run `led.clear`, then `audio.speak` with `{"text":"Herald is ready."}`.
6. Persist defaults with `settings.set`: `{"volume":75,"brightness":100}`.
   Reboot with `device.reboot`, reconnect and read `settings.get` again.

`demo.show` with `{"volume":75}` prepares six speech clips, gives a 20-second
recording countdown, and runs lights, narration and an original synthesized
melody. Default demo volume is 100; normal saved speech volume starts at 75.
There is no microphone, voice wake word, display or Bluetooth speaker output.
If Muse claims no RGB hardware, refresh its live device description and have
it identify the actual command-tool error instead of inferring from old notes.

## 6. Future updates

Build a newer version using the same private overlay and signing key:

```sh
BOARD_FIRMWARE_VERSION=999.0.2 tools/board.sh hw678-n16r8-herald build
```

Privately serve `build-hw678-n16r8-herald/muse-gadget.bin` at a URL reachable
from the gadget and invoke `device.ota` with `{"url":"YOUR_PRIVATE_IMAGE_URL"}`.
That image contains credentials: do not place it in public GitHub Releases.
Keep version numbers increasing (this profile starts at 999.0.1). Reconnect,
refresh the description and verify `device.info` afterward. Existing OTA
rollback validates the image once Link returns; USB remains a recovery option.

The SDK uses a public development signing key, not production key isolation.
Keep the same key for compatible updates. Secure boot, flash encryption, NVS
encryption and pairing eFuse authentication are disabled in this profile.
Changing that trust model is outside this prototype's build recipe.

## Other agents and CLI tools

The optional [Herald MCP bridge and CLI](../../herald-mcp/README.md) provide
authenticated HTTPS control independently of Muse. Provision a private LAN
certificate/token, enable the LAN overlay, and follow the bridge setup guide.

## Development checks

With ESP-IDF activated, build first, then run:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

The registration-size test uses the real serializer; the LED lifecycle harness
runs the real pattern manager with AddressSanitizer. See the command reference
for Wi-Fi reconnect session behavior, cancellation, brightness and diagnostics
limits. See [publication notes](../../HERALD_RELEASE.md) for the secret audit.

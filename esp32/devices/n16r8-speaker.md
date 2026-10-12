# HW678 N16R8 speaker base profile

For the complete two-LED build, use [Herald setup](herald.md). This page describes
the original speaker base profile inherited by Herald.

Hardware identified from the user's PCB photos (`HW678 V0.0.0`) and live
chip/flash queries: ESP32-S3, 16 MB quad flash, 8 MB octal PSRAM, UART bridge.
No vendor schematic or onboard RGB LED pin has been established. The onboard status LED stays
disabled; the external RGB LEDs use their own driver. The current firmware's BOOT input is GPIO0 and console UART is GPIO43/44.

NS4168 wiring, confirmed against the amplifier's printed labels:

| Amplifier | Bare board |
|---|---|
| G | GND |
| V | 3V3 |
| BCL | GPIO4 |
| LRC | GPIO5 |
| DIN | GPIO6 |

Disconnect USB before wiring. Speaker wires remain on the amplifier's speaker
connector; neither speaker terminal goes to board ground. Begin at low volume.

Build `sdkconfig.defaults;devices/sdkconfig.n16r8-speaker` for `esp32s3`.
`tools/board.sh n16r8-speaker build` uses a separate build directory. Set the SDK
token privately before building. Keep the existing partition geometry and NVS.

This profile adds a 300 ms quiet startup test tone, `audio.test` (optional volume
0-100, default 75), `audio.play` (required HTTP(S) URL, optional volume), and
`audio.speak` (text up to 1,000 UTF-8 bytes, optional volume, default 75).
Playback accepts PCM16 WAV, 8-48 kHz, mono/stereo, up to 2 MB, without redirects.
Both channels are mixed to mono and replicated to the I2S channels. Concurrent
playback returns busy. Results are asynchronous and bound to the originating
Muse connection generation. The tunnel and existing discovery/OTA remain enabled.

`audio.speak` synthesizes speech through the configured text-to-WAV endpoint.
Muse can invoke it for spoken replies. An explicit command volume overrides the
firmware default. Microphone input is not implemented. `audio.play` does not
accept MP3, AAC, or compressed WAV.

## External RGB LEDs and narrated showcase

`CONFIG_HOMEHUB_EXTERNAL_RGB_TEST=y` enables the external LED driver and show for
the user's common-cathode CHANZON LED. Connect its longest lead to GND and each
color lead through its own 330-ohm resistor to GPIO15, GPIO16, and GPIO17.
The user confirmed GPIO15 is blue, GPIO16 green, and GPIO17 red.
The second LED also needs three individual 330-ohm series resistors and its
common cathode connected to shared GND. The user confirmed the second LED mapping:
GPIO7 is green, GPIO8 is blue, and GPIO9 is red.
Both LEDs remain off at boot. `demo.show` (optional volume, default 100) prepares
six narrated speech clips from the configured TTS service before a 20-second recording countdown.
It performs RGB sequencing, complementary color blends, audio-reactive brightness,
and a short original melody synthesized directly on the ESP32. The narration
explains the actual wiring, PWM, I2S, server synthesis, and development process.
Speech uses the requested show volume; melody amplitude is intentionally lower.
LED brightness is capped at 65 percent duty. The LEDs switch off afterward,
including on a failed preparation. The speaker lock prevents overlapping shows
and normal speech. A command timeout of 30 minutes accommodates slow TTS requests.
The USB console also accepts `>demo`; use the COM/USB-to-serial port for the console. Normal speech still defaults to 75.

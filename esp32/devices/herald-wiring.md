# Herald wiring

This wiring was exercised on a **HW678 V0.0.0 ESP32-S3 N16R8** board with two
USB-C connectors and rows of pins: 16 MB flash and 8 MB octal PSRAM. It is not
a Seeed XIAO. Pin numbers below are the GPIO labels printed on the PCB, not
positions counted along the header. No vendor schematic has been established;
other boards sold as “ESP32-S3 N16R8” may have different layouts.

## Parts

- HW678 ESP32-S3 N16R8 development board and a USB data cable.
- NS4168 I2S amplifier module with `G`, `V`, `BCL`, `LRC`, `DIN` labels.
- Optional emakefun ZTS663 I2S MEMS microphone module (ZTS6631 chip) with a
  5-pin PH2.0 connector: BCLK, LRC, DATA, +5V, GND.
- Passive speaker compatible with your amplifier module's specified load.
  The original speaker's impedance/power rating was not recorded; check yours.
- Two common-cathode RGB LEDs. The tested parts were CHANZON 8 mm, whose bag
  marks the longest lead negative. Other LEDs can have a different pin order.
- Six 330 Ω resistors, one per color leg; 1/4 W is sufficient for this wiring.
- Breadboard or other insulated connections, jumper wires, and a shared GND rail.

Disconnect USB before wiring. A capacitor does not replace a series resistor.
Never connect the color legs directly to a GPIO without their resistors.

## Speaker

| NS4168 pin | ESP32 board pin |
|---|---|
| G | GND |
| V | 3V3 |
| BCL (bit clock) | GPIO4 |
| LRC (left/right clock) | GPIO5 |
| DIN (audio data) | GPIO6 |

Connect the passive speaker to the amplifier's speaker connector or its marked
speaker output terminals. Neither speaker output goes to ESP32 ground: the
amplifier drives both terminals. The tested setup powers the amplifier from
3V3; do not substitute 5V or an external supply without checking the module.
Begin with a low-volume `audio.test`. If loud playback resets the board, lower
the volume and investigate power delivery instead of increasing the gain.

## Microphone (optional)

The microphone is a separate I2S input on the ESP32's second I2S port, so
it shares no pins with the speaker or the LEDs and needs no branching of
the speaker's clock wires.

| ZTS663 module pin (PH2.0 J1) | ESP32 board pin |
|---|---|
| 1 BCLK | GPIO14 |
| 2 LRC | GPIO13 |
| 3 DATA | GPIO10 |
| 4 +5V | 5V |
| 5 GND | GND |

Power the module from the board's 5V pin, not 3V3: the module carries its
own 5V-to-3.3V regulator (ME6211C33) feeding the ZTS6631, whose supply
range is 1.6-3.6V. There are no VDD or L/R pins to wire: the module's
SELECT line is pulled low and CONFIG floats, which hardwires it to I2S
format, left channel. The chip sends 24-bit samples (MSB-aligned in a
32-bit slot) at -26 dBFS sensitivity with 64 dB SNR.

Firmware support is the `mic.level` command (Kconfig `HOMEHUB_MIC`),
which captures about a second at 16 kHz and reports RMS, peak and dBFS.
That is level metering only: the audio is measured and discarded, with
no recording, streaming or wake word.

## LEDs

| LED index | Red leg through 330 Ω | Green leg through 330 Ω | Blue leg through 330 Ω | Common cathode |
|---|---|---|---|---|
| 0 | GPIO17 | GPIO16 | GPIO15 | GND |
| 1 | GPIO9 | GPIO7 | GPIO8 | GND |

Each table entry represents its own resistor. The common cathodes share ground
with the amplifier. If the board's GND pins are occupied, connect one GND pin
to a breadboard ground rail and distribute ground from that rail. Do not use an
ordinary GPIO as a substitute ground pin.

```text
GPIO17 ── 330 Ω ── LED 0 red leg       GPIO9 ── 330 Ω ── LED 1 red leg
GPIO16 ── 330 Ω ── LED 0 green leg     GPIO7 ── 330 Ω ── LED 1 green leg
GPIO15 ── 330 Ω ── LED 0 blue leg      GPIO8 ── 330 Ω ── LED 1 blue leg
                     common ──┐                        common ──┐
ESP32 GND ── ground rail ──────┴──────── amplifier G ────────────┘
```

Identify the common lead and colors from your LED's datasheet or a resistor-
limited test, not a universal left-to-right lead order. Firmware uses ordinary
PWM LEDs, not WS2812/NeoPixel addressable LEDs. The onboard status LED is unused;
`HOMEHUB_LED_BACKEND_NONE` does not disable these two external RGB LEDs.

## USB and first test

Use the connector marked **COM** for flashing and startup logs. The other
connector is native USB; it can flash the chip, but this profile logs over UART.
The observed bridge is VID:PID `1a86:55d3`; native USB is `303a:1001`. Port names
vary, so explicitly select the board when more than one is attached.

After [setup](herald.md), use `led.set` with one color at a time on each index.
Verify red, green and blue before running a pattern, then use `led.clear`.
The two LEDs start off; a quiet startup beep verifies speaker initialization.

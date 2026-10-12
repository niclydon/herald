# Synchronized audio and LEDs

Herald with CONFIG_HOMEHUB_SHOW=y exposes show.play and show.stop over
Muse Link and authenticated LAN/MCP. The HW678 profile enables it;
xiao-s3 remains a compatibility build alias. Existing speaker wiring
and both external RGB LEDs are reused.

## Parameters

show.play requires audio_url and timeline. Audio must be reachable HTTPS
PCM16 WAV, 8–48 kHz, mono/stereo, at most 2 MiB. URLs have a 2,048-byte
limit and cannot contain credentials. The entire file is fetched and
validated before playback; loops reuse that buffer.

Timeline holds 1–32 phases in nondecreasing start_ms order. Each requires:
- pattern: chase, alternate, fade, or sparkle.
- colors: 1–16 arrays of three integer channels, 0–255.
- start_ms: integer 0–86,400,000.
- duration_ms: integer 1–86,400,000.

Optional phase speed_ms is 20–60,000 (default 300), brightness is 0–100
(default 100). Optional show volume is 0–100 (default 75); loop defaults
to false. These explicit defaults do not inherit saved settings.

Example (replace the URL with your WAV):
```json
{
  "audio_url": "https://example.com/show.wav",
  "volume": 85,
  "timeline": [
    {"colors":[[255,0,0],[0,255,0]], "pattern":"fade",
     "start_ms":0, "duration_ms":6000, "speed_ms":2000},
    {"colors":[[255,0,0],[0,255,0]], "pattern":"alternate",
     "start_ms":6000, "duration_ms":5000, "speed_ms":200},
    {"colors":[[255,255,255],[255,215,0]], "pattern":"sparkle",
     "start_ms":11000, "duration_ms":8000, "speed_ms":150}
  ]
}
```

MCP tools are herald_show_play and herald_show_stop. CLI accepts:
`herald show.play "$(cat show.json)"`. Poll the returned job.

## Timing and overlap

Newest still-active phase wins; ties favor the later array entry.
An older overlapping phase resumes at its original elapsed position if
the newer phase ends first. Gaps hold the last LED state. From firmware 999.0.5, normal non-looping
completion clears both LEDs before sending the completion result. Stops
and loop restarts retain their existing behavior.
Completion waits for audio AND the maximum phase end. Loop repeats both
together after that point.

Audio has its own task; the coordinator selects phases every 10 ms and the
existing LED worker renders every 20 ms. The shared clock starts just
before PCM is queued to I2S. This is software choreography, not
sample-accurate sync; very short phases can be missed by LED refresh.

## Results and cancellation

Link immediately receives {"_async":true}, then exactly one result:
{"ok":true,"payload":{"duration_ms":19000,"stopped":false}}, or an error.
Duration excludes initial download and includes all loop iterations.
LAN/MCP returns a job ID; poll it until done.

One show owns both engines through preparation, playback, and cleanup.
Another show returns a busy error asking for show.stop. Other LED/audio
commands cannot replace its outputs. Existing ordinary LED patterns are
superseded on claim; existing audio must finish or be stopped first.

show.stop has no parameters, acknowledges immediately, cancels audio, and
freezes LEDs. It errors when idle. audio.stop also cancels the entire show,
including loops and LED tails. The original job completes with stopped:true.
An outstanding HTTP operation may take its existing timeout (up to 10 s)
to unwind; cancellation prevents playback from starting. Wait for the job
before starting a replacement or using led.clear to turn lights off.

Future shows only need new parameters, without firmware changes.
Personalized firmware binaries must remain private.

## Validation

Verified on HW678 N16R8 with firmware 999.0.4 on 2026-10-12:
- 237 firmware host tests passed (one skip); 10 MCP tests passed.
- Registration serialized to 7,845 bytes, below 8,192.
- Signed app: 0x161000 bytes in a 0x200000 slot, 31% free.
- Live LAN command discovery advertised both show commands.
- Six-second timeline completed with duration_ms=6000 after a 2.745-second
  WAV, exercising the LED tail after audio ended.
- A second simultaneous show was rejected; stop while idle was rejected.
- Looping playback completed cleanly via both show.stop and audio.stop.
- Wi-Fi remained reachable with zero reconnects; saved settings survived.
- Operator confirmed hearing the audio and seeing both LEDs animate together
  during the six-second hardware test.

The live test fetched the public
[SpeechRecognition English counting fixture](https://github.com/Uberi/speech_recognition/blob/master/examples/english.wav)
directly over HTTPS. It was validated locally as PCM16 mono, 44.1 kHz,
242,148 bytes. The source fixture is not bundled here.

Firmware 999.0.5 adds automatic LED clearing on normal completion. The
two-phase host test asserts clearing precedes the result callback and that
stops, failures, and loop restarts do not clear. The admin-enabled build
passed 239 host tests (one skip) and 11 MCP tests.

After USB installation of the admin-enabled 999.0.5 build, the live
six-second show completed in 6,009 ms. The operator confirmed both LEDs
turned off automatically; no separate led.clear was sent. Live MCP
discovery exposed herald_device_ota, and an empty-URL request reached the
firmware OTA dispatcher and returned invalid_params. A full OTA transfer
was not performed in this validation.

A subsequent end-to-end MCP OTA test reinstalled the signed 999.0.5 image
with force=true from a temporary LAN HTTPS host. MCP reported applied;
Herald rebooted from ota_0 into ota_1, reached OTA state VALID (2), and
retained volume 75 and brightness 100. No USB flashing or Muse invocation
was used for that test. Temporary firmware hosting, DNS, and the narrowly
scoped firewall exception were removed afterward.

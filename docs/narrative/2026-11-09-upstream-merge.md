# Upstream merge, 2026-11-09: muse-gadget-sdk at 197033c

The fork's `herald` branch took its sixth upstream merge: the 30 commits
facebookincubator/muse-gadget-sdk added between 463bd2a (Nov 6, the base
of the Nov 6, 2026 merge) and 197033c (Nov 9). The merge commit is
d3a3a1d, on top of 7908032. The Furnace mirror clone
(`~/projects/muse-gadget-sdk-mirror`) fast-forwarded the same range,
463bd2a to 197033c. This pull was also the second intake under the
estate pattern: niclydon/muse-gadget-sdk-estate advanced the same range
in the same run (its upstream-main fast-forwarded and its main merged;
both documented patches re-verified, its one voice_audio.py conflict
resolved as a union of upstream's 48 kHz playback upsampler with the
estate primer patch).

The headliner is security: #346, the Linux client now refuses the root
CAs distrusted after the eMudhra key compromise (a new `tls.py` builds
the context from the common root store minus the distrusted roots, with
a test vector bundle). Around it: #338 validates a saved pairing before
connecting, #329 drops control records that exceed the JSON parser
depth instead of failing on them, and the WebSocket/Noise session fixes
(#317 disables WebSocket compression for Noise sessions, #318
reconnects sooner after a healthy session, #320 yields between buffered
receive batches, #360 runs the compression test over plain ws://). New
voice boards: reSpeaker XVF3800 (#341), Waveshare ESP32-S3-Touch-LCD-1.85B
(#332) and 3.5 (#161), the ePaper-1.54G push-to-talk board (#240), and
DFRobot UNIHIKER K10 (#362). Upstream also restructured voice-board
selection into an explicit Kconfig choice plus a CMake selector
(voice_board_select.cmake), added an interactive terminal chat on Linux
(#315), a 48 kHz playback upsampler (#324), and the Bambu Lab (#224,
#337), Tuya (#61), SinricPro (#363) and Amazon Echo (#67) skills.
123 files changed, 11,671 insertions, 345 deletions.

## The four conflicts

Nine files had changed on both sides; five auto-merged, including
esp32/main/main.c and esp32/tools/board.sh. Four conflicted:

- `esp32/devices/README.md`, the Features section, resolved the same
  way as Oct 29, Nov 3, Nov 4 and Nov 6: Herald's feature paragraph
  kept, upstream's updated single feature matrix taken (it adds the
  reSpeaker XVF3800, Waveshare S3 1.85B, Waveshare S3 LCD 3.5 and
  UNIHIKER K10 columns), the stale Herald-side table dropped.
- `esp32/main/CMakeLists.txt`, the voice-board source selection.
  Upstream replaced the old backend-derived if-chain with an include
  of its new voice_board_select.cmake, which has no driver for
  Herald's HW678 I2S speaker board. Resolved as a union in the
  standing Herald-first shape: under CONFIG_HOMEHUB_I2S_SPEAKER the
  branch still appends voice_board_herald.c; every other board goes
  through upstream's selector include, which appears exactly once.
- `esp32/main/Kconfig.projbuild`, the HOMEHUB_VOICE definition.
  Upstream narrowed it to `depends on SPIRAM` with defaults only for
  the Voice PE ring and reSpeaker Lite backends, as part of the
  explicit voice-board choice. Taken as upstream wrote it: Herald's
  enablement survives because sdkconfig.hw678-n16r8-herald already
  sets CONFIG_HOMEHUB_VOICE=y explicitly, and upstream's own
  test_voice_board_selection.py pins this exact shape.
- `esp32/main/led_status.c`, the ring-renderer guard. Herald's side
  carried its VOICE_VOLUME_MS define block above the guard; upstream
  had already moved that define into the voice state block at the top
  of the file (auto-merged, exactly one define remains) and
  reformatted the guard to one line. Resolved by taking upstream's
  guard line and dropping the now-duplicate Herald block.

A git grep verifies no conflict markers remain anywhere in the tree.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the pre-merge branch
(empty diff against 7908032) and `ble_central.c` appears exactly once
in `esp32/main/CMakeLists.txt` (line 74), under Herald's
`CONFIG_HOMEHUB_BLE_CENTRAL` conditional append (line 73). The app.c
dispatch union is intact (app.c did not conflict): Herald's six `ble.*`
names (scan, connect, discover, read, write, disconnect, lines
1976-1978) are handled first by `ble_central_command` (line 1979)
under `CONFIG_HOMEHUB_BLE_CENTRAL`, then upstream's
`ble_commands_handles`/`ble_commands_handle` (lines 2012-2013) serves
its extra names, and the early BLE-stack guard keeps the Nov 3 union
form `(CONFIG_MUSE_ENABLED && CONFIG_SPIRAM &&
!CONFIG_MUSE_BOARD_CROWPANEL_ADVANCE_43) ||
CONFIG_HOMEHUB_BLE_CENTRAL` (line 2968). The OTA path is untouched
(`esp32/main/ota.c` and `ota.h` unchanged against the pre-merge
branch). Our CMake arrangements stand: Herald's LAN certificate/key
block (CONFIG_HOMEHUB_LAN_CONTROL, lines 77 and 192) stands ahead of
upstream's fast_gcm (line 201) and ble_coex (line 213) blocks, with
upstream's P4-only `esp_driver_jpeg` append at line 145, and
`esp32/tools/board.sh` keeps Herald's entries (hw678-n16r8-herald,
n16r8-speaker, BOARD_PRIVATE_CONFIG, firmware-version args) intact.

## Verification

The Linux workflow (`.github/workflows/linux.yml`) was replicated on
Furnace against the merged tree: a Python 3.12 venv, `pip install
pytest .` in `linux/`, then pytest, plus ShellCheck 0.9.0 on
`linux/install.sh`. Result: 784 passed, 2 skipped, ShellCheck clean,
exit 0. The Nov 6 baseline was 660 passed; the growth is upstream's
new Linux test surface in this range (terminal chat, TLS, link
receive/transport, spoken turns). Upstream's new
`esp32/tests/test_voice_board_selection.py`, which pins the Kconfig
and CMake voice-board shapes this merge had to resolve, also passes
against the merged tree: 8 passed, 30 subtests passed.

ESP32 compile is NOT verified. No ESP-IDF toolchain exists on Furnace,
and GitHub Actions on the niclydon account is billing-blocked until
Dec 1, 2026, so no CI run can stand in for it; the bench consequence
from the Nov 4 merge still stands (the next bench build must run
ESP-IDF v6.0.3). Flash and bench validation belong to the Herald
lane's next bench session. Nothing was flashed.

# Upstream merge, 2026-11-04: muse-gadget-sdk at 270e653

The fork's `herald` branch took its fourth upstream merge: the 17 commits
facebookincubator/muse-gadget-sdk added between b166d88 (Nov 2, the base
of the Nov 3, 2026 merge) and 270e653 (Nov 4). The merge commit is
b86b3bc, on top of 39a5c87. The Furnace mirror clone
(`~/projects/muse-gadget-sdk-mirror`) fast-forwarded the same range,
b166d88 to 270e653.

The batch is headlined by two linked changes. The TLS commit (#313)
narrows the certificate bundle from the full set to the common root set
(`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN`) and turns on
cross-signed verification, so chains that reach a common root through a
cross-sign (Google Trust Services via GlobalSign, Sectigo via USERTrust)
still verify while ESP-IDF's two legacy local roots drop out. The
ESP-IDF move (#316, below) is its prerequisite. Around them: full-screen
replies with a text-size setting on small screens (#254), two caption
lines in the compact band (#312), speaking gestures and facial
expressions while a reply plays (#305), plain short replies asked for
spoken turns (#314), a voice-capture resample when the microphone has
no 24 kHz mode (#304), a voice-note mic backlog kept to whole samples
(#300), the SpotPear ESP32-AI-1.28 board (#296), an optional MIPI-CSI
camera plus hardware JPEG decode for `draw_url` on the Waveshare P4
3.5 (#193, #192), minimum modem Wi-Fi power saving on the reTerminal
E1002 (#298), the Linux client reading replies from /chat/subscribe
(#100) and tolerating undecodable Wi-Fi tool output (#303), and the
web-flasher gaining a device-settings page and full English/Japanese
i18n (#295).

## The ESP-IDF v6.0.3 move (#316), diffed for build impact

Upstream moved every ESP-IDF pin from v6.0.1 to v6.0.3: the two CI
containers in `.github/workflows/esp32.yml`, the fallback install paths
in `esp32/tools/board.sh`, `esp32/tools/muse/board.sh` and
`esp32/tools/coexist/bench/run_variant.sh` (the generic
`esp-idf-v6` / `esp-idf` fallbacks are retained; a directory literally
named `esp-idf-v6.0.1` is no longer probed), and the setup docs. Three
substantive pieces ride along:

- A hard configure-time gate in `esp32/cmake/validate_config.cmake`:
  with cross-signed bundle verification on (which #313's
  `sdkconfig.defaults` change now enables by default), any ESP-IDF
  older than v6.0.3 fails the build with a FATAL_ERROR, because those
  releases leak heap on every TLS handshake with it on. In effect
  v6.0.3 is now required for default builds, not just recommended.
- The pairing host test now links whatever TF-PSA-Crypto archives the
  IDF build produced (v6.0.3 ships TF-PSA-Crypto 1.1) instead of a
  fixed 1.0 archive list.
- The Waveshare P4 3.5 board constructs its LCD panel IO directly
  with `psram_dma_direct`, because v6.0.3's SPI panel IO no longer
  sends from PSRAM by DMA unless asked.

Component requirements change only additively and not in our path:
`esp32/main/CMakeLists.txt` appends `esp_driver_jpeg` to
GADGET_REQUIRES for the `esp32p4` target only, and
`esp32/components/muse/idf_component.yml` adds the SpotPear board to
two existing dependency rules plus an `esp_video ~2.5.0` dependency
gated on the P4 3.5 board. No version pin Herald relies on moved.

Effect on our builds and the bench flow: nothing in Herald's own
arrangements conflicts. `esp32/main/CMakeLists.txt`,
`esp32/main/Kconfig.projbuild`, `esp32/tools/board.sh` and
`esp32/cmake/validate_config.cmake` all auto-merged; Herald's board
entries (hw678-n16r8-herald, n16r8-speaker, the BOARD_PRIVATE_CONFIG
and firmware-version plumbing in board.sh) sit in different hunks from
upstream's one-line path change and are intact. Herald does not touch
`sdkconfig.defaults`, so the CMN bundle and the cross-signed flag land
exactly as upstream set them. The one real consequence is operational:
the next bench build must run ESP-IDF v6.0.3. A v6.0.1 install will
now fail at configure time under the new gate, and per upstream's
README note each existing build directory's generated `sdkconfig`
should be deleted when moving an install across.

## The one conflict

One file conflicted: `esp32/devices/README.md`, the Features section,
resolved the same way as Oct 29 and Nov 3: Herald's feature paragraph
kept, upstream's updated single feature matrix taken (it adds the
SpotPear column), the stale Herald-side table dropped. A git grep
verifies no conflict markers remain anywhere in the tree.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the pre-merge branch
and `ble_central.c` appears exactly once in `esp32/main/CMakeLists.txt`
(line 74), under Herald's `CONFIG_HOMEHUB_BLE_CENTRAL` conditional
append. The app.c dispatch union is intact: Herald's six `ble.*` names
(scan, connect, discover, read, write, disconnect) are handled first by
`ble_central_command` under BLE_CENTRAL, then upstream's
`ble_commands_handles`/`ble_commands_handle` serves its extra names,
and the early BLE-stack guard keeps the Nov 3 union form with
upstream's CrowPanel exclusion on the Muse path. The OTA path is
untouched by this merge (`esp32/main/ota.c` and `ota.h` are unchanged
against the pre-merge branch). Our CMake arrangements stand: Herald's
LAN certificate/key block (CONFIG_HOMEHUB_LAN_CONTROL) is intact ahead
of upstream's fast_gcm and ble_coex blocks, with upstream's new
P4-only `esp_driver_jpeg` append landed alongside, and
`noise_control.cpp` auto-merged with Herald's registrations preserved.

## Verification

The Linux workflow (`.github/workflows/linux.yml`) was replicated on
Furnace against the merged tree: a Python 3.12 venv, `pip install
pytest .` in `linux/`, then pytest, plus ShellCheck on
`linux/install.sh`. Final result: 660 passed, 2 skipped, and
ShellCheck clean. The Nov 3 baseline was 603 passed, 2 skipped; the
suite grew with upstream's new tests (full-reply, voice-note backlog,
send-and-wait, SpotPear PCM, P4 camera, mbedtls bundle guard). One
earlier full run showed a single failure in
`test_eof_silence_does_not_renew_call_deadlines[overall]`, a
0.35-second-deadline timing test; it passed on isolated re-runs and
the clean full re-run above supersedes it, so it is recorded as a
timing flake under shared-host load, not a merge effect.

ESP32 compile is NOT verified, for the standing reasons: GitHub
Actions has never run on this fork (enabling it is the owner's
repo-settings call; it was not enabled and no PR was opened), and no
ESP-IDF toolchain exists on Furnace. Flash and bench validation belong
to the Herald lane's next bench session. Nothing was flashed.

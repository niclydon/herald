# Upstream merge, 2026-11-13: muse-gadget-sdk at 7b302e3

The fork's `herald` branch took its ninth and tenth upstream
merges, sequenced through b11908c because the Nov 12 merge had
landed locally but never pushed: its suite run hung (see
Verification) and the push waited on it. The Nov 12 merge commit
is cfcbe43, on top of e63e68b, taking the eleven commits between
9d8cca1 and b11908c (#333 gadgets describe themselves, #348
Vesync humidifier skill, #248 Waveshare AMOLED-1.75C flasher
entry, #230 Wi-Fi reconnect cancellation, #181 avatar
restore/status, #334 reply progress indicator, #374 muted
caption sizing, #369 fast e-ink mode, #372 dead-connection
recovery, #323 ALIENTEK BOX2 board, #366 LilyGO T-Display-S3
AMOLED board). The Nov 13 merge commit is 5b39fa7, on top of
cfcbe43, taking the eight commits between b11908c and 7b302e3:
#388 (simulator: define _DEFAULT_SOURCE so LVGL's usleep() is
declared on glibc), #389 (Linux: pin the oldest CI leg to
Ubuntu 24.04 for Python 3.9), #390 (ESP32 video: write exactly
each panel piece's rows, also at band seams), #375 (Linux video
calls: a new rtc/ package, video_call.py and voice_video.py),
#391 (tinyh264: read only within the access unit each decode
call is given), #392 (ESP32 video: keep shown pictures until
any parameter set or IDR slice), #340 (Linux: report a voice
call's transcript, lifecycle and audio offsets), and #397
(ESP32 StopWatch: leave the internal RAM the pairing and Link
tasks need, routing the Muse VM lookup task through
helper_task so it can run from PSRAM-backed stacks). The
Furnace mirror clone fast-forwarded the same ranges to
7b302e3. This was also the fifth and sixth intakes under the
estate pattern: niclydon/muse-gadget-sdk-estate advanced the
same ranges in the same runs (its Nov 12 merge dfea0e3 and Nov
13 merge 7517815, sync log at f7da1a7; both documented patches
re-verified, Patch 1 grep 2 and Patch 2 grep 3).

## The merges

The Nov 12 merge had four conflicts, all resolved as unions:
README (Herald's paragraph plus upstream's new board columns),
esp32/main/CMakeLists.txt (Herald's LAN cert/key block ahead of
upstream's new device_guide block), main.c (Herald's includes
kept, link_events.h added at upstream's position), and
noise_control.cpp (Herald's device.health wording plus
upstream's connectivity-events clause and the new device.guide
command). The Nov 13 merge had NO conflicts: upstream's app.c
changes (#397's helper_task routing, in the hatch_vm_task
region) auto-merged away from Herald's dispatch region, and
the remaining 52 files were upstream-only. A git grep verifies
no conflict markers remain anywhere in the tree at 5b39fa7.

## Standing decisions, verified after the merges

Herald's `ble_central.c/.h` are byte-identical to the pre-merge
branch (empty diff against e63e68b across both merges) and
`ble_central.c` appears exactly once in
`esp32/main/CMakeLists.txt` (line 76), under Herald's
`CONFIG_HOMEHUB_BLE_CENTRAL` conditional (line 75). The app.c
dispatch union is intact: Herald's six `ble.*` names (scan,
connect, discover, read, write, disconnect, lines 1976-1978)
are handled first by `ble_central_command` (line 1979) under
`CONFIG_HOMEHUB_BLE_CENTRAL`, then upstream's
`ble_commands_handles`/`ble_commands_handle` (lines 2012-2013)
serves its extra names, and the early BLE-stack guard keeps
the union form `(CONFIG_MUSE_ENABLED && CONFIG_SPIRAM &&
!CONFIG_MUSE_BOARD_CROWPANEL_ADVANCE_43) ||
CONFIG_HOMEHUB_BLE_CENTRAL` (line 2971). Herald's OTA
customizations are intact (ota.c is unchanged against e63e68b
across both merges, because upstream did not touch it): the
esp_heap_caps include (line 87), the ota_start heap log line
(line 362), and the 8192-byte OTA task stack (line 365) stand,
and the diff of the merged ota.c against upstream is
byte-identical to the pre-merge diff against upstream (same
md5), i.e. exactly Herald's pre-existing changes; ota.h has no
diff against upstream. Our CMake arrangements stand in
`esp32/main/CMakeLists.txt`: Herald's LAN blocks
(CONFIG_HOMEHUB_LAN_CONTROL at line 79, with the herald_lan.c
append at line 80, and the cert/key block at lines 194-199)
stand ahead of upstream's device_guide block (lines 202-206),
fast_gcm block (line 208 onward) and ble_coex block (line 220
onward), and `esp32/tools/board.sh` keeps Herald's entries
(hw678-n16r8-herald, BOARD_PRIVATE_CONFIG) intact.

## Verification

The Linux workflow was replicated on Furnace against the
merged tree: a Python 3.12 venv, `pip install pytest .` in
`linux/`, then pytest, plus ShellCheck on `linux/install.sh`.
One test was deselected before the run:
tests/test_send_and_wait.py::test_a_stalled_transport_cannot_hold_the_caller_past_its_deadline
hangs indefinitely in this environment; it hangs identically
on pristine upstream at 7b302e3 (verified on the mirror clone,
90 s timeout), and it is the same hang that stalled the Nov 12
lane's suite for 21 hours before that process was killed
during this intake. With it deselected: 931 passed, 9 failed,
5 skipped in 307.98 s; ShellCheck clean, exit 0. All 9
failures are in the timing-sensitive family that also fails on
pristine upstream in this environment (the test_voice_events.py
held-PCM/held-fragment tests, whose failure counts vary run to
run on both trees, and
test_video_call.py::test_live_playback_is_paced_in_device_sized_frames,
which passes isolated): none is in a file Herald or this merge
changed behaviorally, and the estate tree's run of the same
suite shows the same family. The Nov 11 baseline was 784
passed, 2 skipped; the collected count has since grown by the
two batches' new Linux tests (video calls, voice events, RTC).

ESP32 compile is NOT verified. No ESP-IDF toolchain exists on
Furnace, and GitHub Actions on the niclydon account is
billing-blocked until Dec 1, 2026, so no CI run can stand in
for it. Flash and bench validation belong to the Herald lane's
next bench session. Nothing was flashed.

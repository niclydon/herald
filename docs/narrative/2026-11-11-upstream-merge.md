# Upstream merge, 2026-11-11: muse-gadget-sdk at 9d8cca1

The fork's `herald` branch took its eighth upstream merge: the four
commits facebookincubator/muse-gadget-sdk added between 55cbf48
(Nov 10, the base of the Nov 10, 2026 merge) and 9d8cca1 (Nov 10).
The merge commit is 76acc39, on top of 918e4c5. The Furnace mirror
clone (`~/projects/muse-gadget-sdk-mirror`) fast-forwarded the same
range, 55cbf48 to 9d8cca1. This pull was also the fourth intake under
the estate pattern: niclydon/muse-gadget-sdk-estate advanced the
same range in the same run (its upstream-main fast-forwarded to
9d8cca1 and its main merged at 1575f64, sync log at 59a2ecd; both
documented patches re-verified, Patch 1 grep 2 and Patch 2 grep 3,
Linux suite 792 passed, 2 skipped).

The four commits are #359, #347, #373, and #377. #359 halves the
FNK0104B LVGL draw buffers: `DRAW_BUF_LINES` goes from 32 to 16, so
two 240-row RGB565 buffers take 15 KB of internal RAM instead of
30 KB, leaving an 8 KB block free once paired so the Muse VM
credentials lookup task can start (at 32 rows every push-to-talk
turn failed with CAN'T REACH MUSE). #347 adds the
gadget-homewhiz-laundry skill, a cloud-only, read-only skill for
HomeWhiz washers and dryers (Arcelik, Beko, Blomberg) that lists
cycle state through the user's HomeWhiz account, and lists it in
the catalog (53 skills). #373 makes the Tuya cloud skill ask for
explicit confirmation before switching heaters, valves, garage or
door openers, irrigation, or sockets and switches whose load is
unknown, judged by device category rather than name, keeps the API
key in a user-only file for REST calls as well as the subscriber,
and marks the skill as cloud only in the catalog. #377 applies the
same confirmation rule to the Tuya Wi-Fi skill, using the same
category list as the cloud skill so the two stay in sync; an
unfamiliar or unknown category also needs confirmation. 5 files
changed, 117 insertions, 6 deletions; no linux/ files were touched.

## The merge

No files had changed on both sides in this range, and there were
no conflicts. The merge touches only the five upstream files:
`esp32/components/muse/boards/board_fnk0104b.c`,
`skills/CATALOG.md`, the new
`skills/gadget-homewhiz-laundry/SKILL.md`, and the two Tuya skill
files.

A git grep verifies no conflict markers remain anywhere in the
tree.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the pre-merge
branch (empty diff against 918e4c5) and `ble_central.c` appears
exactly once in `esp32/main/CMakeLists.txt` (line 74), under
Herald's `CONFIG_HOMEHUB_BLE_CENTRAL` conditional append
(line 73). The app.c dispatch union is intact (app.c was not
touched by this merge): Herald's six `ble.*` names (scan, connect,
discover, read, write, disconnect, lines 1976-1978) are handled
first by `ble_central_command` (line 1979) under
`CONFIG_HOMEHUB_BLE_CENTRAL`, then upstream's
`ble_commands_handles`/`ble_commands_handle` (lines 2012-2013)
serves its extra names, and the early BLE-stack guard keeps the
Nov 3 union form `(CONFIG_MUSE_ENABLED && CONFIG_SPIRAM &&
!CONFIG_MUSE_BOARD_CROWPANEL_ADVANCE_43) ||
CONFIG_HOMEHUB_BLE_CENTRAL` (line 2968). Herald's OTA
customizations are intact (ota.c and ota.h are unchanged against
the pre-merge branch, because upstream did not touch them in this
range): the esp_heap_caps include (line 87), the ota_start heap
log line (line 362), and the 8192-byte OTA task stack (line 365)
stand, and the diff of the merged ota.c against upstream is
exactly Herald's pre-existing changes. Our CMake arrangements
stand: Herald's LAN certificate/key block
(CONFIG_HOMEHUB_LAN_CONTROL, lines 77 and 192) stands ahead of
upstream's fast_gcm (line 199) and ble_coex (line 211) blocks,
with upstream's P4-only `esp_driver_jpeg` append at line 145, and
`esp32/tools/board.sh` keeps Herald's entries
(hw678-n16r8-herald, BOARD_PRIVATE_CONFIG, firmware-version args)
intact.

## Verification

The Linux workflow (`.github/workflows/linux.yml`) was replicated
on Furnace against the merged tree: a Python 3.12 venv,
`pip install pytest .` in `linux/`, then pytest, plus ShellCheck
0.9.0 on `linux/install.sh`. Result: 784 passed, 2 skipped in
36.98s, ShellCheck clean, exit 0, matching the Nov 10 baseline
exactly (this range touched no linux/ files).

ESP32 compile is NOT verified. No ESP-IDF toolchain exists on
Furnace, and GitHub Actions on the niclydon account is
billing-blocked until Dec 1, 2026, so no CI run can stand in for
it; the bench consequence from the Nov 4 merge still stands (the
next bench build must run ESP-IDF v6.0.3). Flash and bench
validation belong to the Herald lane's next bench session.
Nothing was flashed.

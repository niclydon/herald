# Upstream merge, 2026-11-10: muse-gadget-sdk at 55cbf48

The fork's `herald` branch took its seventh upstream merge: the single
commit facebookincubator/muse-gadget-sdk added between 197033c
(Nov 9, the base of the Nov 9, 2026 merge) and 55cbf48 (Nov 10).
The merge commit is e5332b7, on top of 187b324. The Furnace mirror
clone (`~/projects/muse-gadget-sdk-mirror`) fast-forwarded the same
range, 197033c to 55cbf48. This pull was also the third intake under
the estate pattern: niclydon/muse-gadget-sdk-estate advanced the
same range in the same run (its upstream-main fast-forwarded and
its main merged with no conflicts; both documented patches
re-verified, Patch 1 grep 2 and Patch 2 grep 3, Linux suite
792 passed, 2 skipped).

The commit is #356, an ESP-Mosaico build fix in two parts. The link
fix: `usb_serial_jtag_vfs_use_driver()` is now called only under
the same condition ESP-IDF uses to build that VFS
(`CONFIG_VFS_SUPPORT_IO && CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED`),
because the Mosaico's console is its UART and the build failed at
link. The Vibe Mode 0.2 part: with a 0.2 checkout of
esp-mosaico-utils, board.sh builds against the 0.2 contract (its
own sdkconfig overlay `sdkconfig.muse-esp-mosaico-vibe2`,
`partitions_mosaico_vibe2.csv` with nvs at 0x200000 and main_app
at 0x210000, and a separate build directory), project.cmake picks
the app-side components from `esp-mosaico-vibe-mode/` when that
directory exists, and `ota_installed_by_vibe_mode()` in
esp32/main/ota.c finds Vibe Mode by its app/test subtype under
Recovery ABI 2 while keeping the factory subtype for ABI 1.
0.1 builds are unchanged. The devices README documents both tool
generations. 12 files changed, 143 insertions, 26 deletions, all
under esp32/; no linux/ files were touched.

## The merge

Two files had changed on both sides (esp32/devices/README.md and
esp32/main/ota.c) and both auto-merged; there were no conflicts.
In the README, Herald's additions (the Herald paragraph and
HW678 N16R8 row in Supported devices, the Herald line in
Features, the build-table row) sit in different sections from
upstream's Mosaico changes, and all survive. In ota.c, upstream's
change is confined to `ota_installed_by_vibe_mode()`; Herald's
OTA customizations (the esp_heap_caps include, the ota_start heap
log line, the 8192-byte OTA task stack) are untouched, the diff of
the merged ota.c against upstream is exactly Herald's pre-existing
changes, and ota.h is unchanged.

A git grep verifies no conflict markers remain anywhere in the
tree.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the pre-merge
branch (empty diff against 187b324) and `ble_central.c` appears
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
CONFIG_HOMEHUB_BLE_CENTRAL` (line 2968). Our CMake arrangements
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
0.9.0 on `linux/install.sh`. Result: 784 passed, 2 skipped,
ShellCheck clean, exit 0, matching the Nov 9 baseline exactly
(this range touched no linux/ files). The two esp32 test files
upstream changed in this range, `esp32/tests/test_link_ota.py`
and `esp32/tests/test_muse_console_install.py`, also pass against
the merged tree: 8 passed, 7 subtests passed.

ESP32 compile is NOT verified. No ESP-IDF toolchain exists on
Furnace, and GitHub Actions on the niclydon account is
billing-blocked until Dec 1, 2026, so no CI run can stand in for
it; the bench consequence from the Nov 4 merge still stands (the
next bench build must run ESP-IDF v6.0.3). Flash and bench
validation belong to the Herald lane's next bench session.
Nothing was flashed.

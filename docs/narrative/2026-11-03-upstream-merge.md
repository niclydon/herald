# Upstream merge, 2026-11-03: muse-gadget-sdk at b166d88

The fork's `herald` branch took its third upstream merge: the 48 commits
facebookincubator/muse-gadget-sdk added between 1db6a26 (the base of the
Oct 29, 2026 merge) and b166d88 (Nov 2). The merge commit is 1db3b09, on
top of b09436a. The Furnace mirror clone
(`~/projects/muse-gadget-sdk-mirror`) fast-forwarded the same range,
1db6a26 to b166d88.

What arrived is mostly new boards and polish around Herald rather than
changes to Herald itself. Eleven-plus board commits: Waveshare panels
(S3 Touch AMOLED 1.8, 1.43 and 1.43C, Touch LCD 1.9, 1.46, 3.49 V2,
P4-WIFI6 Touch LCD 3.5, S3 2.06, C6 AMOLED 1.8 and the S3 ePaper 1.54G),
Elecrow's CrowPanel Advance 4.3, the ESP32-2432S028 Cheap Yellow Display,
an ESP32-C3 with a 0.42-inch OLED, the ATK-DNESP32S3-BOX, and the
Deotaland RoRoLee. Behind them sit fixes in Herald's class of hardware:
a long talk press on PWR no longer trips the AXP2101 power-off on
1.75C, CoreS3 and 2.16 (#285), the BOX-3 LVGL stack raised so the Wi-Fi
list cannot overflow it (#292), the screen staying on while a reply
plays (#286), and two connection fixes, reading all waiting inbound
frames each session turn (#294) and sending small writes on the Muse
connection at once with TCP_NODELAY (#293). Features: the Ecowitt
gateways skill (#251) plus Tesla and Google Nest thermostat skills,
Vietnamese UI language and captions, SenseCAP Watcher wheel review and
click-to-sleep (#156), owner-extensible Linux gadget commands (#85),
and a run of Linux fileops, executor, and doctor robustness fixes.

## The two conflicts

Two files conflicted. `esp32/main/app.c`, one hunk: upstream added a
CrowPanel Advance 43 exclusion to the early BLE-stack guard
(`CONFIG_MUSE_ENABLED && CONFIG_SPIRAM && !CONFIG_MUSE_BOARD_CROWPANEL_ADVANCE_43`),
and Herald's side carries its own widening of the same guard for BLE
central. The resolution is the union in the Oct 21 spirit:
`(CONFIG_MUSE_ENABLED && CONFIG_SPIRAM && !CONFIG_MUSE_BOARD_CROWPANEL_ADVANCE_43) || CONFIG_HOMEHUB_BLE_CENTRAL`,
so Herald's central path is unchanged and the Muse path takes
upstream's CrowPanel exclusion exactly.

`esp32/devices/README.md`, the Features section, resolved the same way
as Oct 29: Herald's feature paragraph kept, upstream's updated single
feature matrix taken (it adds the new boards above), the stale Herald
table dropped. A git grep verifies no conflict markers remain anywhere
in the tree.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the pre-merge branch
and `ble_central.c` appears exactly once in `esp32/main/CMakeLists.txt`,
under Herald's conditional append. The app.c dispatch union is intact:
Herald's six `ble.*` names are handled first by `ble_central_command`,
then upstream's `ble_commands` serves its extra names. The OTA path is
untouched by this merge (`esp32/main/ota.c` and `ota.h` are unchanged
against the pre-merge branch). Our CMake arrangements stand: the Herald
LAN certificate/key block is its own complete conditional ahead of
upstream's fast_gcm and ble_coex blocks, and `noise_control.cpp`
auto-merged with Herald's LAN and show/ble command registrations
preserved on top of upstream's changes.

## Verification

The Linux workflow (`.github/workflows/linux.yml`) was replicated on
Furnace against the merged tree: a Python 3.12 venv, `pip install
pytest .` in `linux/`, then pytest, plus ShellCheck on
`linux/install.sh`. Result: 603 passed, 2 skipped, and ShellCheck
clean. The Oct 29 equivalent run was 428 passed; the suite grew with
upstream's new Linux commands, fileops, and send-message tests.
ShellCheck warnings on the esp32 board helper scripts are unchanged
from pristine upstream (same 21 findings on both trees); the workflow's
ShellCheck gate covers `linux/install.sh` only, and it is clean.

ESP32 compile is NOT verified, for the standing reasons: GitHub
Actions has never run on this fork (enabling it is the owner's
repo-settings call; it was not enabled and no PR was opened), and no
ESP-IDF toolchain exists on Furnace. Flash and bench validation belong
to the Herald lane's next bench session. Nothing was flashed.

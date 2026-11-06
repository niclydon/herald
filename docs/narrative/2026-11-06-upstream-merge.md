# Upstream merge, 2026-11-06: muse-gadget-sdk at 463bd2a

The fork's `herald` branch took its fifth upstream merge: the 3 commits
facebookincubator/muse-gadget-sdk added between 270e653 (Nov 4, the base
of the Nov 4, 2026 merge) and 463bd2a (Nov 6). The merge commit is
6dbc687, on top of c0bc0d2. The Furnace mirror clone
(`~/projects/muse-gadget-sdk-mirror`) fast-forwarded the same range,
270e653 to 463bd2a. This pull was also the first intake under the
estate pattern: niclydon/muse-gadget-sdk-estate advanced the same
range in the same run (its upstream-main fast-forwarded and its main
merged clean, both documented patches re-verified).

The headliner is #266, keep Wi-Fi when a device token is revoked or
refused. When `fetch_vms` refused the stored device token and the
forced refresh was refused too, the firmware used to erase the tokens
and run the full setup reset, which also erased the saved Wi-Fi
networks; explicit unpairs erased Wi-Fi as well. Now the outcomes
split by refusal kind: a 401 (revoked, replaced by a newer pairing, or
already used) erases only the device tokens via `config_unpair()`,
keeps saved Wi-Fi and reopens pairing, so re-pairing needs only the
app; a 403, a 5xx or a network failure keeps the tokens and backs off,
on a refusal stamp separate from the refresh scheduler's. Explicit
unpairs and setup-failure cleanup also keep Wi-Fi via
`config_unpair()`. The 5-second hold and the menu/Settings "Reset
pairing" remain the user's factory reset and still forget Wi-Fi. Boot
handling follows the same split, and a paired Link with no VM session
now retries from the heartbeat at most once a minute. The change
carries a dedicated host test suite (`test_link_token_refusal.py`
against the real `config_store.c`).

Around it: the VIEWE SmartRing-Plus board (#162, a new board file,
display and power headers, device page and sdkconfig), and voice calls
showing a WORKING state while Muse is still on it (#278, in
`muse_voice_call_core` and the pixel avatar).

## The one conflict

Five files had changed on both sides (README.md, esp32/AGENTS.md,
esp32/README.md, esp32/devices/README.md, esp32/main/app.c); four
auto-merged, including app.c despite upstream's 229-line token-refusal
rework. One file conflicted: `esp32/devices/README.md`, the Features
section, resolved the same way as Oct 29, Nov 3 and Nov 4: Herald's
feature paragraph kept, upstream's updated single feature matrix taken
(it adds the SmartRing-Plus column), the stale Herald-side table
dropped. A git grep verifies no conflict markers remain anywhere in
the tree.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the pre-merge branch
(empty diff against c0bc0d2) and `ble_central.c` appears exactly once
in `esp32/main/CMakeLists.txt` (line 74), under Herald's
`CONFIG_HOMEHUB_BLE_CENTRAL` conditional append (line 73). The app.c
dispatch union is intact after upstream's rework: Herald's six `ble.*`
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
block (CONFIG_HOMEHUB_LAN_CONTROL, lines 77 and 188) is intact
alongside upstream's fast_gcm and ble_coex blocks and the P4-only
`esp_driver_jpeg` append (line 145), and `esp32/tools/board.sh` keeps
Herald's board entries (hw678-n16r8-herald, n16r8-speaker,
BOARD_PRIVATE_CONFIG, firmware-version args) intact.

## Verification

The Linux workflow (`.github/workflows/linux.yml`) was replicated on
Furnace against the merged tree: a Python 3.12 venv, `pip install
pytest .` in `linux/`, then pytest, plus ShellCheck 0.9.0 on
`linux/install.sh`. Result: 660 passed, 2 skipped, ShellCheck clean,
exit 0. That is exactly the Nov 4 baseline, as expected: this upstream
range touched no `linux/` files, so the suite neither grew nor shrank.

ESP32 compile is NOT verified, for the standing reasons: GitHub
Actions has never run on this fork (enabling it is the owner's
repo-settings call; it was not enabled and no PR was opened), and no
ESP-IDF toolchain exists on Furnace. The bench consequence from the
Nov 4 merge still stands: the next bench build must run ESP-IDF
v6.0.3. Flash and bench validation belong to the Herald lane's next
bench session. Nothing was flashed.

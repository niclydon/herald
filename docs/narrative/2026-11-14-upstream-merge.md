# Upstream merge, 2026-11-14: muse-gadget-sdk at f2c702b

The fork's `herald` branch took its eleventh upstream merge,
taking the five commits between 7b302e3 and f2c702b: #399
(Linux: use the system trust store, and refuse to run while it
trusts distrusted roots), #402 (the device.ota command
removed), #409 (Linux: install a pinned, verified uv and
harden the installer), #408 (CI: free disk space before
ESP-IDF builds), and #407 (ESP32: experimental optional
WebAssembly gadget apps, a new muse_apps component, off by
default under CONFIG_MUSE_WASM_APPS). The merge commit is
690cb0e, on top of 39bf084. The Furnace mirror clone
fast-forwarded the same range to f2c702b. This was also the
seventh intake under the estate pattern:
niclydon/muse-gadget-sdk-estate advanced the same range in
the same run (merge fdd45f8, sync log at b15a485; both
documented patches re-verified, Patch 1 grep 2 and Patch 2
grep 3).

## The merge

ONE conflict, in esp32/main/app.c, and it was comment-only:
above ota_verify_task, Herald's comment ("Validate through
Muse, or an authenticated LAN client when LAN admin is
enabled") against upstream's #402 rewrite of its own older
comment. The code beneath auto-merged correctly, keeping
Herald's LAN reachability term
(herald_lan_has_authenticated_client under
CONFIG_HOMEHUB_LAN_CONTROL) alongside upstream's text, so
the resolution kept Herald's comment, which is the one that
describes the merged function. Everything else auto-merged,
including upstream's app.c muse_apps dispatch hooks, the
noise_control.cpp changes from #407, and the ota.c/ota.h
comment updates from #402. A git grep verifies no conflict
markers remain anywhere in the tree at 690cb0e.

## Standing decisions, verified after the merge

Herald's `ble_central.c/.h` are byte-identical to the
pre-merge branch (empty diff against 39bf084) and
`ble_central.c` appears exactly once in
`esp32/main/CMakeLists.txt` (line 76), under Herald's
`CONFIG_HOMEHUB_BLE_CENTRAL` conditional (line 75). The
app.c dispatch union is intact: Herald's six `ble.*` names
(scan, connect, discover, read, write, disconnect, lines
1992-1994) are handled first by `ble_central_command` (line
1995) under `CONFIG_HOMEHUB_BLE_CENTRAL`, then upstream's
`ble_commands_handles`/`ble_commands_handle` (lines
2028-2029) serves its extra names, and the early BLE-stack
guard keeps the union form `(CONFIG_MUSE_ENABLED &&
CONFIG_SPIRAM && !CONFIG_MUSE_BOARD_CROWPANEL_ADVANCE_43)
|| CONFIG_HOMEHUB_BLE_CENTRAL` (line 2992). Herald's OTA
customizations are intact: upstream #402 touched ota.c and
ota.h only in comments (device.ota reworded to ota_start),
and the added/removed line set of the merged ota.c diff
against upstream f2c702b is identical to the pre-merge diff
against 7b302e3, i.e. exactly Herald's pre-existing changes
(the esp_heap_caps include, the ota_start heap log line,
and the 8192-byte OTA task stack); ota.h has no diff
against upstream. Our CMake arrangements stand in
`esp32/main/CMakeLists.txt`: Herald's LAN blocks
(CONFIG_HOMEHUB_LAN_CONTROL at line 79, with the
herald_lan.c append at line 80, and the cert/key block at
lines 196-203) stand ahead of upstream's device_guide
block (lines 204-208), fast_gcm block (line 210 onward)
and ble_coex block (line 222 onward), and
`esp32/tools/board.sh` keeps Herald's entries
(hw678-n16r8-herald, BOARD_PRIVATE_CONFIG) intact and
unchanged. herald_lan.c/.h are unchanged against the
pre-merge branch.

## Verification

The Linux workflow was replicated on Furnace against the
merged tree: a Python 3.12 venv, `pip install pytest .` in
`linux/`, then pytest, plus ShellCheck on
`linux/install.sh`. One test was deselected before the run:
tests/test_send_and_wait.py::test_a_stalled_transport_cannot_hold_the_caller_past_its_deadline
hangs indefinitely in this environment; it hangs
identically on pristine upstream (verified Nov 13 on the
mirror clone at 7b302e3, 90 s timeout), and it is the same
hang that stalled the Nov 12 lane's suite for 21 hours.
With it deselected: 981 passed, 0 failed, 5 skipped in
94.15 s; ShellCheck clean, exit 0. The Nov 13 baseline on
this branch was 931 passed, 9 failed, 5 skipped (all 9 in
the timing-sensitive held-PCM/held-fragment family in
test_voice_events.py, which also fails on pristine
upstream with counts varying run to run); that family
passed in full in this run, and there were no new
failures. The estate tree's run of the same suite in this
intake: 985 passed, 4 failed (exactly the known Patch 2
test_playback_flush_* family, the same 4 as Nov 13), 5
skipped. The esp32/tests host harness was not re-attempted:
it does not complete on Furnace within 1200 s (Nov 13:
24 passed, no failures, at the cap) and is not a gate.

ESP32 compile is NOT verified. No ESP-IDF toolchain exists
on Furnace, and GitHub Actions on the niclydon account is
billing-blocked until Dec 1, 2026, so no CI run can stand
in for it. Flash and bench validation belong to the Herald
lane's next bench session. Nothing was flashed.

# Upstream merge, 2026-10-29: muse-gadget-sdk at 1db6a26

The fork's `herald` branch took its second upstream merge: the 21 commits
facebookincubator/muse-gadget-sdk added between e6fe517 (the base of the
Oct 21, 2026 merge) and 1db6a26 (Oct 28). The merge commit is 52f23e1, on
top of 3ed7903.

What arrived is mostly reach and polish around Herald rather than changes
to Herald itself. The headline is a Roku gadget: local control of Roku TVs
and players over ECP, as a new skill (#123). Behind it sit a reconnect fix
that preserves command concurrency limits (#205), read-only Linux gadget
doctor diagnostics (#212), realtime voice for Linux and StopWatch (#275),
public Linux hooks for gadgets built on the SDK (#142), readable CJK
captions on 320x240 screens (#134) plus the follow-up fix for the sticky
board test after the CJK font macro change (#279), an e-ink layer on
FreeInk that drops LVGL from e-paper boards (#260), Seeed XIAO ESP32-S3
1.47 Touch Display board support (#255), and a run of smaller fixes:
sizing the BLE reassembly buffer to the message (#136), routing USB VFS
output through the installed console driver (#185), treating a cut-off
API response as a transport failure (#176), and rejecting incomplete
bench screenshots (#174).

## The one conflict, and the two artifacts behind it

Only one file conflicted: `esp32/devices/README.md`, in the Features
section. Resolving it surfaced something the Oct 21 merge record did not
know: that merge's resolution commit (3ed7903) had itself committed
literal conflict markers into this file. The committed text held the
Herald feature paragraph and two stale feature tables on one side of the
markers and the then-current upstream table on the other. Upstream's new
version updates that same table (the XIAO S3 1.47 column, the FreeInk
Sticky rework), so the resolution keeps Herald's paragraph, takes
upstream's updated table as the single feature matrix, and drops both
stale tables and every marker. Herald's other insertions in the file (the
supported-devices row, the build-table row) were outside the conflict and
are unchanged.

The same artifact was committed in `esp32/main/CMakeLists.txt`, where the
markers left the `CONFIG_HOMEHUB_LAN_CONTROL` block unclosed: as
committed, the file could not configure. A follow-up commit (cd795a3)
resolves it as a union in the same spirit as the Oct 21 decisions: the
Herald LAN certificate/key block stands as its own complete conditional,
followed by upstream's fast_gcm and ble_coex blocks, each complete. The
Oct 21 CMake decision is untouched: `ble_central.c` still appears once,
under Herald's conditional append, and Herald's `ble_central.c/.h`
implementation is kept, as is the app.c dispatch union. Nothing in the 21
commits touched `ble_central` or the OTA path this time; `noise_control.cpp`
auto-merged.

## Verification

The Linux workflow (`.github/workflows/linux.yml`) was replicated on
Furnace against the merged tree: `pip install pytest .` in `linux/`, then
pytest, plus ShellCheck on `linux/install.sh`. The clean full run is 428
passed; ShellCheck is clean. A first run, made while two unrelated ROCm
builds were saturating the host, flaked once on a quiet-frame timing test
(`test_voice_quiet_input`); that test passes 3 of 3 in isolation on both
the merged tree and a pristine checkout of upstream 1db6a26, and the full
suite passed end to end on re-run, so the flake is load, not the merge.
The Oct 21 merge's equivalent run was 218 passed; the suite has roughly
doubled with upstream's voice work.

ESP32 remains unverified, for the standing reasons: GitHub Actions has
never run on this fork (the fork enablement gate is the owner's call),
no ESP-IDF toolchain exists on Furnace, and the esp32 workflow's host
tests need that toolchain. A supplementary plain-pytest pass over
`esp32/tests` was started and stopped after about 17 minutes of harness
compilation under load; it is not the CI shape and proves nothing either
way. Flash validation belongs to the next bench session with the device
powered, owned by the Herald chat. Nothing was flashed.

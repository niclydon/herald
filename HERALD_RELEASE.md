# Herald validation and publication record

Prepared for source publication on 2026-10-12. This record covers the Herald
firmware, board profile, documentation and MCP/CLI implementation. Personalized
firmware and private provisioning material are excluded from publication.

## What is included

- Canonical `hw678-n16r8-herald` profile; deprecated `xiao-s3` helper compatibility.
- Two RGB LEDs, I2S speaker, WAV playback, external text-to-WAV speech, narrated
  showcase, remote Wi-Fi/device controls, persistent settings and diagnostics.
- [Setup](esp32/devices/herald.md), [wiring](esp32/devices/herald-wiring.md),
  [command reference](esp32/devices/herald-remote-management.md) and a blank
  private configuration template. Demo narration is generic for other builders.
- Upstream Apache-2.0 attribution and third-party licenses retained. This is a
  community extension, not an official Meta hardware product.

## Secret audit

Gitleaks **8.30.1**, release archive checked against its published SHA256 list,
scanned all reachable Git refs (106 commits at preparation) and an export of
tracked plus non-ignored untracked files. Generated builds and ignored local
files are outside the export. The audit script also refuses known private or
generated artifacts if they appear in that publishable file list.

A separate local exact-value check compared the current private overlays' credential/endpoint values, LAN token and TLS private key against **1,328 historical blobs** and the
publishable worktree: **no matches**. Values and private configuration are not
included in this report or the repository. This check is additional evidence,
not a claim that pattern scanners can prove the absence of every possible secret.

Reviewed scanner findings are restricted to exact content hashes in
[.herald-audit-allowlist.json](.herald-audit-allowlist.json):

| Finding | Disposition |
|---|---|
| Upstream `esp32/dev_signing_key.pem` | Intentionally public development key; never a production signing secret |
| Two fixed WebSocket handshake nonces | Public protocol sample nonce, not authentication credentials |
| Python `private_key` type annotation | Code, not embedded key material |
| ESP32/Linux pairing test vectors | Explicitly synthetic public test data |
| Pairing harness SDK token | Synthetic all-A unit-test token |

Exceptions require matching rule, file path and SHA256 of the entire file.
There is no blanket directory or secret-rule exclusion. Modified fixture files
require review again. A custom Muse SDK token rule supplements Gitleaks defaults.
A negative test injected a temporary synthetic token, verified audit failure
and confirmed the token value was absent from output, then removed the probe.

To repeat, install Gitleaks 8.x and run from the repository root:

```sh
python3 tools/audit_public.py
# Or: GITLEAKS=/path/to/gitleaks python3 tools/audit_public.py
```

The script prints only finding locations/rules and counts. Temporary redacted
scanner reports are deleted on exit. Re-run immediately before publication.
Do not bypass findings by broadly extending the allowlist.

Never publish personalized firmware, flash/NVS backups, generated sdkconfig,
private overlays, logs or tokens. Ignore rules cover common local artifacts;
`git add -f` can override them and is not part of this release procedure.

## Validation

- Clean `tools/board.sh hw678-n16r8-herald build` under ESP-IDF v6.0.1, without
  a private overlay. SDK token and speech URL verified empty in generated config.
- Application size **0x161000 (1,445,888 bytes)**, smallest slot **0x200000**,
  **31% free**. Both board overlays checked against generated configuration.
- Host suite: **233 tests run, no failures, one skipped** (host PSA Crypto).
- Full registration serializer: **5,324 bytes**, below 8,192-byte limit.
- Personalized **999.0.2** firmware with the optional authenticated HTTPS LAN
  endpoint built and was installed on the verified Herald board. Boot reached
  Link registration, without a panic or reboot loop. Its app has the size above.
- The publication pass rebuilt and reran host tests after making two command
  descriptions independent of the prototype TTS host name. That wording-only
  change has not been flashed onto the running device.
- MCP: **8 integration tests passed**, covering real stdio and HTTPS MCP,
  TLS hostname/authentication checks, parameter validation and private config permissions.
- Live hardware MCP test: speech physically heard by the owner; the four-second
  LED pattern reported successful completion, but was not visually observed in
  this test. Earlier two-LED tests were visually confirmed.
- Live ownership conflict, duplicate request handling and cancellation checks passed.
- Codex and Claude user-level stdio integrations installed; Claude reports connected.
- Manual tool-discovery evaluation supplied in `herald-mcp/evaluation.xml`;
  this is not a completed agent benchmark.

## MCP and CLI

[herald-mcp](herald-mcp/README.md) includes a shared TypeScript client, CLI,
stdio MCP server, optional authenticated HTTPS Streamable HTTP server, and
private certificate/token provisioning. It routes through the existing firmware
command dispatcher. No supported external Muse invocation API was identified
in this checkout, so the optional LAN endpoint provides independent access.

The shared HTTPS MCP server is implementation/fixture-tested, not deployed as
an always-running service. The device HTTPS endpoint and local stdio integration
are live. Reboot, OTA and Wi-Fi reconnect require explicit admin opt-in in both
firmware and client; they are disabled on the new LAN path by default. Existing
Muse controls remain available. LAN ownership coordinates LAN clients only;
Muse can still supersede their effects. Jobs and leases do not survive reboot.

Private OTA output is kept outside the repository as `herald-999.0.2.bin`.
It contains personalized credentials and must not be included in a public release.

## Publication scope

The public fork is [niclydon/herald](https://github.com/niclydon/herald), forked
from `facebookincubator/muse-gadget-sdk` with upstream history retained.
The Herald source branch includes firmware, documentation, tests and audit tools.
No firmware binaries, credentials, private device logs, workspace photos or demo
videos are included. The MCP package is not published to npm.

Repeat the audit on the committed tree before pushing. Future changes need the
same review; this record is evidence for this preparation, not a standing clean
bill for later commits. No hosted TTS service or cloud entitlement is supplied.

# Herald MCP, CLI and client

Control Herald's two RGB LEDs, speaker, settings and diagnostics from MCP agents
or shell scripts, independently of Muse's command tools. Muse stays connected.

```text
Codex / Claude / another MCP client → stdio bridge → authenticated HTTPS → Herald
Shell / application                → shared client → authenticated HTTPS → Herald
Remote MCP client → authenticated HTTPS MCP bridge → authenticated HTTPS → Herald
```

Firmware reuses the existing Link command dispatcher. LAN jobs use a reserved
result namespace so their completions cannot reach an unrelated Muse session.
This package stays inside the SDK fork for now; it has not been published to npm.

## Install

Use Node.js 22+ and OpenSSL, then from this directory:

```sh
npm ci
npm run build
# Optional local package commands; does not publish to npm:
npm link
```

Both `herald` and `herald-mcp` binaries are declared in package.json. Without
linking, use `node dist/cli.js` and `node dist/server.js` respectively.
Dependencies are locked. MCP SDK v2 is used; the server also accepts legacy
client protocol negotiation, exercised in the integration suite.

## Provision the device connection

Generate credentials in a **new directory outside this checkout**:

```sh
node dist/cli.js provision /absolute/private/herald DEVICE_IP_OR_HOSTNAME
```

This writes an EC TLS certificate/key, a random 256-bit bearer token, a private
`client.json`, and an `sdkconfig.lan` overlay. Existing directories are refused
rather than overwriting credentials. Files containing keys/tokens are mode 0600.
The certificate's name is `herald.local`; the client verifies that name using
only the generated trust certificate, even when connecting by IP address.
No system trust-store changes or certificate-verification bypass are used.

Combine `sdkconfig.lan` with your existing private SDK-token/TTS overlay into a
new private file. Build the [Herald profile](../esp32/devices/herald.md) with
`BOARD_PRIVATE_CONFIG` pointing to that combined file and a newer
`BOARD_FIRMWARE_VERSION`. Move the generated build sdkconfig aside first so
new overlay settings apply. The HTTPS listener is optional and disabled unless
`CONFIG_HOMEHUB_LAN_CONTROL=y`; enabling it requires the private PEM paths.

Install using existing OTA or the verified USB procedure. Preserve NVS and
pairing. Firmware images now also contain the LAN token and device TLS key:
keep builds, backups and overlays private. Retain these credentials for future
builds; rotating them requires rebuilding and updating client configuration.

Allow the chosen clients to reach the device on TCP **8443**. Do not port-forward
the gadget to the Internet. Reserve its DHCP address or update the URL in
client.json when its address changes. No Muse API token is needed by this client.

```sh
export HERALD_CONFIG=/absolute/private/herald/client.json
node dist/cli.js describe
node dist/cli.js device.info
```

## Codex and Claude Code

These commands store the private configuration **path**, not its token:

```sh
codex mcp add herald --env HERALD_CONFIG=/absolute/private/herald/client.json -- node /absolute/checkout/herald-mcp/dist/server.js
claude mcp add herald --scope user --transport stdio -e HERALD_CONFIG=/absolute/private/herald/client.json -- node /absolute/checkout/herald-mcp/dist/server.js
```

Use an absolute Node executable path if the desktop app has a different PATH.
Reload MCP connections or start a fresh client session after adding the server.
See [official Codex MCP configuration](https://developers.openai.com/codex/mcp/);
Claude command syntax is also available via `claude mcp add --help`.

Every stdio server process gets its own random ownership ID by default. Only set
`HERALD_OWNER` when deliberately sharing an owner across related operations.
Do not assign the same fixed owner to unrelated agents.

Try asking an agent:

> Use Herald to show blue while you work. When finished, show green and speak
> a one-sentence result. Poll speech jobs until complete and release ownership.
> Use red if the task fails.

Tools use the `herald_` prefix: for example `herald_led_pattern`,
`herald_audio_speak`, `herald_device_info`, and `herald_settings_get`.
`herald_describe` reads live capabilities. `herald_task_status` composes the
working/success/failure light effects and optional speech.

## CLI and library

```sh
herald notify working
# Run your own work or tests here.
herald notify success "Tests passed."
herald led.pattern '{"pattern":"fade","colors":[[255,0,0],[0,0,255]],"duration_ms":4000}'
herald job RETURNED_JOB_ID
herald cancel
```

The CLI's default owner is `cli` so separate shell invocations can manage one
workflow. Concurrent scripts should set different `HERALD_OWNER` values.
Parameters follow the [firmware command reference](../esp32/devices/herald-remote-management.md).
JSON is printed to stdout; failures go to stderr with a nonzero exit status.
A submitted asynchronous job is not a completed operation: poll its ID and check
`state="done"` and `result.ok=true`. Immediate device errors also produce a
nonzero CLI exit. Effects can continue after the CLI exits.

```js
import { HeraldClient, loadConfig } from './dist/client.js';
const herald = new HeraldClient(loadConfig());
try {
  const job = await herald.submit('audio.speak', {text: 'Build complete.'});
  const result = await herald.wait(job.id, 270000);
  console.log(result); // inspect state and result.ok
  await herald.release();
} finally {
  herald.close();
}
```

Long MCP operations return job IDs immediately. `herald_job` polls them;
`herald_cancel` stops audio/LED effects and releases ownership. Cancelling an
MCP request or closing the client does not cancel a device job. Neither cancel
nor audio.stop aborts OTA, scans, or an in-flight TTS HTTP download.

## Ownership, jobs and limits

- The first LAN mutation claims a **120-second lease**. Further mutations and
  owner job polls refresh it. Other LAN owners receive HTTP 409 while it is active.
- Read operations remain available. Lease expiry permits takeover; it does not
  turn off an indefinite effect. Release does not stop effects either; use cancel.
- This is coordination among clients with the same device credential, not a
  security boundary between them. **Muse is outside this lease and can supersede
  LEDs or audio.** Existing hardware busy/supersession rules still apply.
- Request IDs deduplicate identical submissions for their retention window.
  Reusing an ID with different content returns 409. The client never automatically
  retries a mutation after an uncertain connection failure; query the returned
  error's job ID first.
- The device has 32 bounded job slots. Completed results are retained for at
  least 60 seconds and then become eligible for reuse; running jobs are never
  evicted. A full table returns 503. Reboots discard all jobs and leases.
- Request bodies are limited to 24 KiB with show support (4 KiB otherwise); HTTPS has three socket slots. The shared
  client reuses one socket. This is a small gadget, not a high-throughput API.
- TLS uses the per-device self-signed certificate and a separate bearer token.
  Source-only distribution, no secure boot or flash encryption, and the upstream
  public development signing key remain the prototype's trust model.

## Administrative tools

`device.reboot`, `wifi.reconnect` and `device.ota` are disabled over LAN by
default. To enable them, set `CONFIG_HOMEHUB_LAN_ADMIN=y` in the private firmware
overlay **and** `"admin": true` in the private client config. MCP omits these
three tools when client admin is false. The firmware still rejects them if its
own admin option is off. Muse's existing administrative path is unchanged.

Reboot/reconnect/OTA can interrupt polling. Verify `device.info` or `wifi.status`
after reconnecting instead of automatically replaying the operation. An OTA
image URL must remain private because personalized firmware contains credentials.

## Optional shared Streamable HTTP MCP service

`herald-mcp --http` implements an HTTPS MCP endpoint at `/mcp`. It is tested but
is not automatically installed as a daemon. Keep stdio for a single local CLI;
run this service on a host reachable by remote agents when needed.

Set `HERALD_HTTP_CONFIG` to a mode-0600 JSON file with:

```json
{
  "host": "127.0.0.1",
  "port": 9443,
  "certFile": "/private/bridge-cert.pem",
  "keyFile": "/private/bridge-key.pem",
  "allowedHosts": ["bridge.example.internal"],
  "clients": {
    "build-agent": {"token": "REPLACE_WITH_RANDOM_32_BYTE_SECRET", "admin": false}
  }
}
```

Supply a certificate valid for the bridge hostname and trusted by the remote
clients. Bridge TLS is distinct from device TLS. Each named client gets its own
bearer token and ownership ID. Authorization, Host and Origin are checked before
MCP dispatch; request bodies are limited to 64 KiB. No permissive CORS or
unauthenticated HTTP listener is provided. This is pre-shared bearer auth for
managed CLI clients, not an OAuth authorization server or a public MCP service.
Remote clients use `Authorization: Bearer ...`; keep secrets out of command-line
arguments, committed config and logs. Administrative exposure requires all three
switches: firmware, device client configuration, and the named bridge client's
`admin` flag. The remote agent never receives the device's own bearer token.

## Tests and verification

```sh
npm test
# Explicit opt-in hardware test: animates LEDs and speaks one sentence.
HERALD_CONFIG=/private/herald/client.json node test/hardware.mjs
```

The integration suite uses real MCP stdio and Streamable HTTP clients against
a local HTTPS device fixture, including auth, TLS name checking, input validation
and administrative filtering. Firmware host tests compile the real LAN handlers
with ASAN to exercise authentication, body bounds, leases, deduplication and
result ownership. The hardware test verifies those paths against the ESP32;
its logs contain neither the bearer token nor spoken-response credentials.

## BLE peripherals

Central-enabled firmware (999.0.3+) exposes `herald_ble_scan`, `herald_ble_connect`,
`herald_ble_discover`, `herald_ble_read`, `herald_ble_write`, and
`herald_ble_disconnect`. These tools return jobs immediately; poll with
`herald_job`. See the [BLE guide](../esp32/devices/herald-ble.md). Older firmware
rejects these commands; check `herald_describe` before invoking them.

### Synchronized shows

Use `herald_show_play` / `herald_show_stop` in MCP, or CLI
`herald show.play "$(cat show.json)"`.
The [show guide](../esp32/devices/herald-show.md) documents timeline semantics,
limits, cancellation, and a complete example. Poll the returned job for completion.

With LAN administration enabled, an authenticated LAN request after reboot
also validates an OTA image. The MCP client should reconnect and call
`device.info` within five minutes; Muse is not required for that validation.
The administrative option enables OTA, reboot, and Wi-Fi reconnect together.

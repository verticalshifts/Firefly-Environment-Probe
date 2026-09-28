# Configuration

Configuration lives in a single versioned JSON document, persisted to
`/config.json` on LittleFS via `StorageManager` (atomic write: a temp file
is written and renamed over the old one, so a power loss mid-write can't
corrupt it — important since provisioning depends on this file at every
boot). See `src/config/ConfigManager.h` for the source of truth; this file
documents each field.

Change it via the Settings page, the first-boot provisioning page, or
directly with `POST /api/config` (see [api.md](api.md)) — all three go
through the same `ConfigManager::update()` validation path.

## Schema

`configVersion` is bumped whenever the schema changes; the current schema is
version `2` (bumped from `1` when `wifiAuthMode`/`wifiUsername`/
`wifiEapPassword` were added). There's no real migration logic beyond
version-stamping — a config written by an older firmware simply parses with
the new fields defaulting (e.g. `wifiAuthMode` defaults to `"personal"`),
and `ConfigManager::begin()` bumps `configVersion` forward in place on
mismatch.

| Field | Type | Default | Notes |
|---|---|---|---|
| `deviceName` | string | `"Environment Probe"` | Shown in the dashboard header and used to derive the mDNS hostname if `mdnsHostname` is blank |
| `wifiSsid` | string | `""` | Empty = not yet provisioned, device stays in AP mode |
| `wifiPassword` | string | `""` | WPA2-Personal PSK; never returned by `GET /api/config`; ignored when `wifiAuthMode` is `"enterprise"` |
| `wifiAuthMode` | `"personal"` \| `"enterprise"` | `"personal"` | Selects WPA2-Personal (`wifiSsid`+`wifiPassword`) vs WPA2-Enterprise/PEAP-MSCHAPv2 (`wifiSsid`+`wifiUsername`+`wifiEapPassword`) |
| `wifiUsername` | string | `""` | EAP identity *and* username (same value used for both) when `wifiAuthMode` is `"enterprise"`; not a secret, returned by `GET /api/config` |
| `wifiEapPassword` | string | `""` | EAP/MSCHAPv2 password; never returned by `GET /api/config`; only used when `wifiAuthMode` is `"enterprise"` |
| `useStaticIp` | bool | `false` | |
| `staticIp` / `staticGateway` / `staticSubnet` / `staticDns` | string | `""` / `""` / `"255.255.255.0"` / `""` | Only used when `useStaticIp` is true |
| `wifiConnectAttempts` | int | `3` | Full connect attempts (fresh scan + `WiFi.begin()` each, not polls of one attempt), spread across a fixed ~60s total budget, before falling back to provisioning AP; validated to 1–10 |
| `authUsername` | string | `"admin"` | Dashboard Basic Auth username |
| `authPassword` | string | *(auto-generated on first boot)* | Never returned by `GET /api/config`; see `/api/provisioning-info` |
| `mdnsHostname` | string | `""` | Blank = slugified `deviceName` |
| `sensorType` | `"DHT11"` \| `"DHT22"` | `"DHT11"` | |
| `sensorGpio` | int | `0` | `0` = platform default (see [hardware.md](hardware.md)) |
| `environmentInterval` | int (seconds) | `10` | How often the sensor is read; validated to 2–3600 |
| `networkInterval` | int (seconds) | `30` | How often *each* ground-probe target is re-checked; validated to 5–3600 |
| `dashboardRefresh` | int (seconds) | `5` | Advisory — the dashboard's own polling cadence; validated to 1–300 |
| `monitors` | array | *(migrated from the old fixed five)* | The network monitors to run. See [Network monitors](#network-monitors) below. Replaces `gatewayTarget` / `pingTarget1` / `pingTarget2` / `dnsDomain` / `httpTarget`, which were removed at schema v3 |
| `probeTimeoutMs` | int | `1500` | Per-probe timeout. Applies to HTTP and port checks; ping and DNS use their library's own fixed timeout |
| `probePacketCount` | int | `1` | Ping attempts per gateway/IP-target probe cycle; validated to 1–20. Each attempt blocks `loop()` for ~1s (standard ping pacing) — raising this raises that stall proportionally, and since Gateway/Target1/Target2 share one interval and become due together, they cascade back-to-back (confirmed live: `5` meant ~15s of combined stalling every `networkInterval`, enough to cause real packet loss to the device itself) |
| `tempHighC` / `tempLowC` | float | `35.0` / `10.0` | Environment alert thresholds |
| `humidityHighPct` / `humidityLowPct` | float | `80.0` / `30.0` | |
| `rssiLowDbm` | int | `-80` | Wi-Fi signal alert threshold |
| `latencyHighMs` | float | `100.0` | Probe latency alert threshold |
| `packetLossHighPct` | float | `10.0` | Probe packet-loss alert threshold |
| `gen2Enabled` | bool | `false` | Opt-in — publishes environment readings to GEN2 Bullseye when true |
| `gen2ServerUrl` | string | `"https://gen2bullseye.com"` | GEN2 host to publish to; firmware always appends `/api/groundprobe`. Cannot be blank |
| `gen2OrgId` | string | `""` | GEN2 org UUID, from GEN2's dashboard |
| `gen2LicenseKey` | string | `""` | Secret, format `gp_<32 hex chars>` from GEN2's Onboarding tab; never returned by `GET /api/config` |
| `gen2MonitorName` | string | `""` | Blank = uses `deviceName`; GEN2 auto-creates a monitor with this name on first successful publish |
| `gen2IntervalS` | int (seconds) | `60` | Minimum spacing between GEN2 HTTPS POSTs, independent of `environmentInterval`; validated to 30–3600 |
| `gen2SyncEnabled` | bool | `false` | Opt-in — pulls monitors GEN2 has dispatched for this license key and applies them locally. Independent of `gen2Enabled`; needs `gen2OrgId` + `gen2LicenseKey` |
| `gen2SyncIntervalS` | int (seconds) | `120` | How often to poll GEN2 for dispatched monitor jobs; validated to 30–3600 |
| `gen2PublishMonitors` | bool | `false` | Opt-in — publishes each monitor's own status to GEN2 under its own name, which is what makes a locally-added monitor appear on GEN2's dashboard |
| `iotgwEnabled` | bool | `false` | Opt-in — also publishes environment readings to a Firefly-CAP100 IoT Gateway. Independent of `gen2Enabled` |
| `iotgwUrl` | string | `""` | Full ingest URL, e.g. `http://192.168.0.24/ajax/iotgw/ingest.php`; must be `http://` or `https://` when enabled |
| `iotgwToken` | string | `""` | Secret bearer token (`iot_…`) from the CAP100's HUB > IoT Gateway page; required when enabled; never returned by `GET /api/config` |
| `iotgwIntervalS` | int (seconds) | `60` | Minimum spacing between gateway POSTs, independent of `environmentInterval`; validated to 10–3600 |
| `otaCheckEnabled` | bool | `false` | Opt-in — periodically checks GitHub Releases for a newer firmware version. Never auto-installs |
| `otaCheckIntervalS` | int (seconds) | `21600` (6h) | How often to check; validated to 300–604800 (5min–7d) |

## Network monitors

Each entry in `monitors` is one check the device runs on the shared
`networkInterval` cycle, one probe per `loop()` call, round-robin:

```json
{"id": "a1b2c3d4", "name": "Gateway", "type": "ping", "target": "", "port": 0, "gen2": false}
```

| Field | Notes |
|---|---|
| `id` | Stable 8-hex-char id, generated by the device. Live results are matched to a monitor by this, never by list position, so removing one monitor can't slide another's history onto it. Omit it when adding a monitor and the device assigns one |
| `name` | Must be unique. This is also the monitor name GEN2 knows it by |
| `type` | `ping` \| `dns` \| `http` \| `port` |
| `target` | IP or hostname (`ping`), domain (`dns`), full URL (`http`), host (`port`). A blank `ping` target means the DHCP-learned gateway |
| `port` | `port` monitors only, 1–65535 |
| `latencyHighMs` | This monitor's "slow above" bar. `0` = inherit (see below). Otherwise at least 10 |
| `gen2` | True if GEN2 dispatched it. Read-only on the device |

**Latency thresholds resolve in three steps**, most specific first:

1. the monitor's own `latencyHighMs`, when non-zero;
2. a per-type default — **HTTP 2000 ms, port 1000 ms** — because those checks
   include connection setup (a TLS handshake on an ESP32 is comfortably over a
   second) and can never meet a ping-sized bar however healthy the endpoint is;
3. the global `latencyHighMs` from Alert Thresholds, which is what `ping` and
   `dns` monitors use since for them the global bar is apt.

`/api/network` reports the resolved value per monitor as `latencyHighMs`, so a
`DEGRADED` row can be explained without re-deriving this. This matters because
a single global threshold made the default HTTPS monitor permanently
`DEGRADED` at ~1300 ms against a 100 ms bar, which — now that overall health is
worst-of-all — pinned the whole device at `WARNING` forever.

`POST /api/config` takes the **whole list** — there is no per-monitor patch.
Sending a shorter list deletes the missing ones. GEN2-owned monitors are
re-attached by the firmware from its own config regardless of what the client
sends, so they can't be edited or deleted through the API even by a client
bypassing the UI.

**Migration from the fixed five.** Before schema v3 the probe targets were
five scalar fields. On first boot at v3 they are converted into five monitors
with the same names, types and targets, and the legacy keys are dropped from
`config.json` at the next save. Nothing is lost and the device looks unchanged
to its user.

**Overall network health** (`network.status` in `/api/status`) is now the
worst status across all monitors — it used to read the Gateway probe alone,
which no longer necessarily exists. A monitor that has never run yet counts as
neither healthy nor faulty, so the device doesn't report itself broken for the
first `networkInterval` after boot. An empty monitor list reports `HEALTHY`:
nothing is being checked, so nothing is failing.

Note the physical status LED does **not** follow this list —
`NetworkHealthIndicator` pings a fixed 8.8.8.8 on its own timer, deliberately
independent of the configurable probes.

## Syncing monitors from GEN2

With `gen2SyncEnabled`, the device polls GEN2's existing ground-probe job
queue and applies what it finds. No GEN2-side changes are required — this uses
the dispatch pipeline GEN2 already has:

- `GET /api/groundprobe/jobs?license_key=&org_id=` claims pending add/remove
  jobs (credentials in the **query string** here, unlike the ingest endpoint
  which takes them in the body).
- `POST /api/groundprobe/jobs/<id>/ack` completes one. The device acks **only
  after** applying a job locally, so a failure leaves the job unacked and GEN2
  redelivers it. A job this firmware can never action (unknown type, missing
  fields) is acked anyway, so it doesn't retry forever.

Two limitations inherited from GEN2's API, worth knowing before relying on it:

1. **Jobs are incremental deltas.** There is no desired-state snapshot,
   version or etag, so a job that never applies leaves the two sides different
   with no way to detect or repair the drift.
2. **Jobs are scoped per license key, not per device** — GEN2 never routes on
   `server_id`, and hands each job to exactly one caller. Two devices sharing a
   license key will take each other's jobs. **Use one license key per device.**

With `gen2PublishMonitors`, each monitor also reports its own status upward
under its own name, which is what makes a locally-added monitor appear on
GEN2's dashboard (GEN2 auto-creates a `probes` row for an unknown monitor
name). One monitor is published per tick, spaced `gen2IntervalS / count`
apart, to stay under GEN2's 120-requests-per-minute-per-IP limit and its
per-probe throttle. The consequence to accept: once a monitor is visible in
GEN2, an admin can dispatch a `remove` for it and the device will honour it.

## GEN2 Bullseye integration

When `gen2Enabled` is true, environment readings (temperature/humidity) are
POSTed to `<gen2ServerUrl>/api/groundprobe` at most once every
`gen2IntervalS` seconds (default 60s), independent of the sensor's own
`environmentInterval` (default 10s). `status` sent is `"UP"` when the
sensor reading is valid, `"DOWN"` otherwise, so GEN2 will correctly alert on
sustained DHT sensor failure, not just network loss.

`gen2ServerUrl` defaults to `https://gen2bullseye.com` but can be pointed at
any GEN2 host (e.g. `https://g2i.batbapps.com`) — the firmware always
appends the fixed `/api/groundprobe` path itself; only the host is
configurable, and it's the one path confirmed to resolve correctly on
every GEN2 host tested (a bare `/groundprobe` only works on some of them —
on others it falls through to the web app's own HTML instead of the API).

`gen2OrgId` and `gen2LicenseKey` come from GEN2's own dashboard (Onboarding
tab, admin-only — there's no self-service device-registration API on GEN2's
side) — not from this device. `gen2MonitorName` (blank = device name)
auto-creates a new monitor row in that org on first successful publish if
the name doesn't already exist there.

`temperature`/`humidity` are accepted by `/groundprobe` (as aliases for
`temperature_c`/`humidity_pct`) and stored correctly, but only render on
GEN2's dashboard on that specific monitor's own detail/history page — not
on the general monitor list/card view, which never shows them regardless of
whether the data arrived.

**The connection uses `setInsecure()` — TLS is not certificate-verified**,
even though this POST carries the secret `gen2LicenseKey`. This was a
deliberate tradeoff, not an oversight: the pinned-root approach tried first
required verifying an RSA-4096 chain, which took ~12s of CPU-bound
signature-verification time on the ESP8266 and blocked the whole firmware
long enough to cause measurable WiFi packet loss (confirmed live). See
`src/telemetry/Gen2Telemetry.h`'s header comment for the full reasoning and
what it would take to revisit this.

## Firefly-CAP100 IoT Gateway (secondary path)

When `iotgwEnabled` is true, environment readings are also POSTed to
`iotgwUrl` at most once every `iotgwIntervalS` seconds:

```
POST http://192.168.0.24/ajax/iotgw/ingest.php
Authorization: Bearer iot_...
Content-Type: application/json

{"temperature": 24.1, "humidity": 51}
```

This is additive — direct GEN2 publishing above is unchanged and
independently controlled by `gen2Enabled`. The gateway buffers, aggregates
and forwards readings to GEN2 itself, so if a device has both paths enabled
*and* the gateway's own GEN2 forwarding is on, GEN2 receives each reading
twice. Pick one path per device unless that duplication is intended.

- Only healthy readings are sent (sensor status `OK`). The gateway contract
  has no status field, so during a DHT fault nothing is sent rather than the
  last-known-good value being reported as live.
- `water_leak` is never sent: this probe has no leak sensor, and the gateway
  carries a sensor's last *reported* leak state forward, so a hard-coded
  `false` would be a fabricated reading.
- Any `2xx` response counts as success. A `401`/`403` is logged as a token
  problem; other errors log the status code and a short response snippet.
- `http://` is the expected transport (LAN-only, token-protected endpoint).
  `https://` also works, without certificate verification — the same trade-off
  as GEN2 above.

## OTA update checking

When `otaCheckEnabled` is true, the device periodically checks
`GET https://api.github.com/repos/<owner>/<repo>/releases/latest` (the repo
is a compile-time constant — `OTA_GITHUB_OWNER`/`OTA_GITHUB_REPO` in
`platformio.ini` — not a Settings field, so an authenticated config change
can't redirect the update source) and compares the release tag against the
compiled `FIRMWARE_VERSION`. **This never installs anything on its own** —
it only makes an available update visible (Settings banner,
`GET /api/ota/status`); a human clicks "Install Update" to actually
trigger `POST /api/ota/install-latest`.

The connection is certificate-verified (`src/ota/GitHubApiRootCA.h`), not
`setInsecure()` — unlike Gen2Telemetry's tradeoff (justified there because
a spoofed sensor reading is low-stakes), a MITM'd update-check response
could point the device at a malicious download, so this stays fully
verified. The actual firmware download (a *different* host and root —
`src/ota/GitHubAssetRootCA.h`) is additionally checksum-verified: the
release description must contain a `SHA256_ESP32:`/`SHA256_ESP8266:` line
(computed and parsed from the same, already-fetched API response — no
extra download), and the install endpoint refuses to proceed without one.
See [release-process.md](release-process.md) for the exact release-authoring
convention this depends on, and `docs/architecture.md`'s "OTA rollback
safety" section for what happens if an installed update fails to boot
cleanly.

## Validation

`ConfigManager::update()` rejects (with a `400` and no change applied) if:

- `sensorType` isn't exactly `"DHT11"` or `"DHT22"`
- `environmentInterval` / `networkInterval` / `dashboardRefresh` /
  `probePacketCount` are outside the ranges in the table above
- `authUsername` would be left empty
- `gen2IntervalS` is outside 30–3600s
- `gen2ServerUrl` would be left empty (a blank submission is silently
  ignored instead, keeping the current value — same convention as
  `gen2LicenseKey`)
- `gen2SyncIntervalS` is outside 30–3600s
- a monitor has an empty name, a duplicate name, or an unknown `type`
- a non-`ping` monitor has an empty `target` (a blank *ping* target is
  meaningful — it means "the DHCP-learned gateway")
- a `port` monitor has no port
- there are more than `MAX_MONITORS` monitors (12 on ESP32, 6 on ESP8266)
- `iotgwIntervalS` is outside 10–3600s
- `iotgwEnabled` is true and `iotgwUrl` doesn't start with `http://` or
  `https://`, or no `iotgwToken` is stored (a blank token submission keeps
  the current one, same convention as `gen2LicenseKey`)
- `otaCheckIntervalS` is outside 300–604800s
- `wifiSsid` is longer than 32 bytes (a hard 802.11 protocol limit)
- `wifiConnectAttempts` is outside 1–10
- `wifiAuthMode` isn't exactly `"personal"` or `"enterprise"`
- `wifiAuthMode` is `"enterprise"` and `wifiUsername` would be left empty
  (while `wifiSsid` is non-empty)

Everything else is accepted as-is — e.g. there's no per-platform GPIO
allowlist enforced server-side, so double-check
[hardware.md](hardware.md)'s "GPIOs to avoid" before setting `sensorGpio`
to something unusual.

## Where it's used

- `sensorType`/`sensorGpio` changes → `EnvironmentManager::reconfigure()`
  re-creates the `DHTSensor` immediately, no reboot needed.
- `wifiSsid`/`wifiPassword`/`wifiAuthMode`/`wifiUsername`/`wifiEapPassword`
  changes → `NetworkManager::applyNewCredentials()` attempts to join the new
  network immediately, falling back to the provisioning AP if it can't.
  `wifiPassword` is only read when `wifiAuthMode` is `"personal"`;
  `wifiUsername`/`wifiEapPassword` only when it's `"enterprise"`.
- Everything else is read live from `ConfigManager::get()` on each use
  (e.g. `NetworkProbe::loop()` re-reads `networkIntervalS` every cycle), so
  most settings take effect without a restart.

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

`configVersion` is bumped whenever the schema changes; Phase 1 ships schema
version `1`. There's no migration logic beyond version-stamping yet since
there's only ever been one version — a future schema change would add a
migration step in `ConfigManager::begin()`.

| Field | Type | Default | Notes |
|---|---|---|---|
| `deviceName` | string | `"Environment Probe"` | Shown in the dashboard header and used to derive the mDNS hostname if `mdnsHostname` is blank |
| `wifiSsid` | string | `""` | Empty = not yet provisioned, device stays in AP mode |
| `wifiPassword` | string | `""` | Never returned by `GET /api/config` |
| `useStaticIp` | bool | `false` | |
| `staticIp` / `staticGateway` / `staticSubnet` / `staticDns` | string | `""` / `""` / `"255.255.255.0"` / `""` | Only used when `useStaticIp` is true |
| `wifiConnectAttempts` | int | `20` | ~500ms per attempt during the initial blocking connect in `setup()` before falling back to provisioning AP |
| `authUsername` | string | `"admin"` | Dashboard Basic Auth username |
| `authPassword` | string | *(auto-generated on first boot)* | Never returned by `GET /api/config`; see `/api/provisioning-info` |
| `mdnsHostname` | string | `""` | Blank = slugified `deviceName` |
| `sensorType` | `"DHT11"` \| `"DHT22"` | `"DHT22"` | |
| `sensorGpio` | int | `0` | `0` = platform default (see [hardware.md](hardware.md)) |
| `environmentInterval` | int (seconds) | `10` | How often the sensor is read; validated to 2–3600 |
| `networkInterval` | int (seconds) | `30` | How often *each* ground-probe target is re-checked; validated to 5–3600 |
| `dashboardRefresh` | int (seconds) | `5` | Advisory — the dashboard's own polling cadence; validated to 1–300 |
| `gatewayTarget` | string | `""` | Blank = use the DHCP-learned gateway IP |
| `pingTarget1` / `pingTarget2` | string | `"8.8.8.8"` / `"1.1.1.1"` | Any IP address |
| `dnsDomain` | string | `"google.com"` | Domain resolved to test DNS |
| `httpTarget` | string | `"https://example.com"` | `http://` or `https://` URL |
| `probeTimeoutMs` | int | `1500` | Per-probe timeout |
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

Everything else is accepted as-is — e.g. there's no per-platform GPIO
allowlist enforced server-side, so double-check
[hardware.md](hardware.md)'s "GPIOs to avoid" before setting `sensorGpio`
to something unusual.

## Where it's used

- `sensorType`/`sensorGpio` changes → `EnvironmentManager::reconfigure()`
  re-creates the `DHTSensor` immediately, no reboot needed.
- `wifiSsid`/`wifiPassword` changes → `NetworkManager::applyNewCredentials()`
  attempts to join the new network immediately, falling back to the
  provisioning AP if it can't.
- Everything else is read live from `ConfigManager::get()` on each use
  (e.g. `NetworkProbe::loop()` re-reads `networkIntervalS` every cycle), so
  most settings take effect without a restart.

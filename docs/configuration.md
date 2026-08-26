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
| `probePacketCount` | int | `5` | Ping attempts per gateway/IP-target probe cycle; validated to 1–20 |
| `tempHighC` / `tempLowC` | float | `35.0` / `10.0` | Environment alert thresholds |
| `humidityHighPct` / `humidityLowPct` | float | `80.0` / `30.0` | |
| `rssiLowDbm` | int | `-80` | Wi-Fi signal alert threshold |
| `latencyHighMs` | float | `100.0` | Probe latency alert threshold |
| `packetLossHighPct` | float | `10.0` | Probe packet-loss alert threshold |

## Validation

`ConfigManager::update()` rejects (with a `400` and no change applied) if:

- `sensorType` isn't exactly `"DHT11"` or `"DHT22"`
- `environmentInterval` / `networkInterval` / `dashboardRefresh` /
  `probePacketCount` are outside the ranges in the table above
- `authUsername` would be left empty

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

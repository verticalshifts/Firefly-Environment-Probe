# REST API

All responses are JSON. Base URL is the device's IP or mDNS hostname, e.g.
`http://envprobe.local` or `http://192.168.1.50`.

## Authentication

Endpoints marked **auth required** use HTTP Basic Auth against the
dashboard username/password (`authUsername`/`authPassword` in config — see
[configuration.md](configuration.md)). A missing/invalid credential gets a
`401` with a `WWW-Authenticate: Basic` header, which browsers turn into a
native login prompt.

Everything else is intentionally open on the LAN — it's read-only
monitoring data with no secrets in it.

## GET /api/status

No auth. Combined snapshot for the dashboard header.

```json
{
  "device": {
    "id": "ENV-ESP32-A1B2C3",
    "name": "Server Room Probe",
    "platform": "ESP32",
    "firmware": "1.0.0",
    "uptimeSeconds": 305732,
    "freeHeap": 148213,
    "bootCount": 4,
    "chipModel": "ESP32-D0WDQ6",
    "flashSize": 4194304,
    "macAddress": "AA:BB:CC:DD:EE:FF",
    "resetReason": "Power on"
  },
  "environment": {
    "temperature": 27.4,
    "humidity": 63.2,
    "sensorType": "DHT22",
    "status": "HEALTHY"
  },
  "network": {
    "connected": true,
    "ssid": "HomeNetwork",
    "ip": "192.168.1.50",
    "rssi": -52,
    "status": "HEALTHY",
    "provisioning": false
  }
}
```

`environment.status` / `network.status` are one of
`HEALTHY | WARNING | CRITICAL | OFFLINE | SENSOR_ERROR` — computed
server-side from the configured thresholds, so the dashboard never
hard-codes them (see [architecture.md](architecture.md)).

## GET /api/environment

No auth.

```json
{
  "temperature": 27.4,
  "humidity": 63.2,
  "valid": true,
  "sensorType": "DHT22",
  "status": "HEALTHY",
  "lastReadingAgeSeconds": 4,
  "thresholds": { "tempHighC": 35, "tempLowC": 10, "humidityHighPct": 80, "humidityLowPct": 30 }
}
```

## GET /api/network

No auth.

```json
{
  "wifi": {
    "ssid": "HomeNetwork",
    "ip": "192.168.1.50",
    "gateway": "192.168.1.1",
    "rssi": -52,
    "channel": 6,
    "connected": true,
    "reconnectCount": 0,
    "provisioning": false,
    "apSsid": "ENVPROBE-A1B2C3"
  },
  "probes": [
    { "label": "Gateway", "target": "192.168.1.1", "reachable": true, "latencyMs": 2.1, "packetLossPercent": 0, "lastProbeSecondsAgo": 12, "status": "UP", "extra": "" },
    { "label": "Probe Target 1", "target": "8.8.8.8", "reachable": true, "latencyMs": 18.4, "packetLossPercent": 0, "lastProbeSecondsAgo": 12, "status": "UP", "extra": "" },
    { "label": "Probe Target 2", "target": "1.1.1.1", "reachable": true, "latencyMs": 22.1, "packetLossPercent": 2, "lastProbeSecondsAgo": 12, "status": "DEGRADED", "extra": "" },
    { "label": "DNS", "target": "google.com", "reachable": true, "latencyMs": 31.0, "packetLossPercent": 0, "lastProbeSecondsAgo": 12, "status": "UP", "extra": "142.250.72.14" },
    { "label": "HTTP/HTTPS", "target": "https://example.com", "reachable": true, "latencyMs": 210.0, "packetLossPercent": 0, "lastProbeSecondsAgo": 12, "status": "UP", "extra": "HTTP 200" }
  ]
}
```

Each probe's `status` is `UP | DEGRADED | DOWN | OFFLINE` (`OFFLINE` = never
run yet, e.g. right after boot).

**HTTPS probe note**: the HTTP/HTTPS probe uses `setInsecure()` — it does
not validate the server's certificate chain. Its job is reachability and
latency, not asserting trust in the endpoint; don't point it at something
where a spoofed/expired cert should be treated as "down".

## GET /api/history?range=1h|6h|24h|7d

No auth. Streamed, hand-built JSON (not one large buffer — see
[architecture.md](architecture.md)) so it stays cheap even on ESP8266.
Downsampled to at most 300 points regardless of range.

```json
{
  "rangeSeconds": 3600,
  "points": [
    { "t": 305100, "temp": 27.1, "hum": 62.8 },
    { "t": 305160, "temp": 27.2, "hum": 62.9 }
  ]
}
```

`t` is **seconds since the device booted**, not wall-clock time (Phase 1
has no NTP/RTC dependency) — use it as a relative axis, or combine with
`device.uptimeSeconds` from `/api/status` to compute "N minutes ago".

## GET /api/config — auth required

Returns the full config with `wifiPassword`/`authPassword` omitted.

## POST /api/config — auth required

Partial update — send only the fields you want to change. On success,
sensor-related changes (`sensorType`/`sensorGpio`) re-initialize the sensor
without a reboot; a `wifiSsid` change triggers a reconnect attempt (falling
back to the provisioning AP if it fails). See
[configuration.md](configuration.md) for the full field list and
validation rules.

```bash
curl -u admin:PASSWORD -X POST http://envprobe.local/api/config \
  -H "Content-Type: application/json" \
  -d '{"environmentInterval": 15, "tempHighC": 32}'
```

```json
{ "status": "ok" }
```

A validation failure returns `400` with `{ "error": "..." }` and leaves the
stored config unchanged.

## POST /api/restart — auth required

```json
{ "status": "restarting" }
```

Restarts ~750ms after the response is sent (long enough for the HTTP
response to flush).

## POST /api/factory-reset — auth required

```json
{ "status": "factory-reset" }
```

Wipes all stored files (Wi-Fi credentials, settings, history) and restarts
into provisioning mode.

## POST /api/ota — auth required

`multipart/form-data` upload of a `.bin` built for the same platform
(`pio run -e esp32` / `-e esp8266`). See the Settings page for the browser
flow, or:

```bash
curl -u admin:PASSWORD -X POST http://envprobe.local/api/ota \
  -F "firmware=@.pio/build/esp32/firmware.bin"
```

```json
{ "status": "ok" }
```

On failure: `{ "status": "error", "message": "<Update library error>" }`
with a non-200 status. The device reboots into the new firmware
automatically on success.

## GET /api/provisioning-info

No auth — but only answers while the device is in its own provisioning AP
(before it has ever joined a real Wi-Fi network). Used by the setup page to
show the auto-generated dashboard password once. Returns `403` once the
device has left provisioning mode.

```json
{
  "deviceId": "ENV-ESP32-A1B2C3",
  "apSsid": "ENVPROBE-A1B2C3",
  "dashboardUsername": "admin",
  "dashboardPassword": "Xk4pQ9mZ2a"
}
```

## POST /provision

No auth (see `/api/provisioning-info` above — same reasoning: only reachable
while the device is in its own open AP). Same body shape as
`POST /api/config`; used by the first-boot setup page to save Wi-Fi
credentials and trigger a connection attempt.

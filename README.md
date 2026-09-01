# ESP Environment & Network Monitoring Probe — Phase 1

A single PlatformIO codebase that turns an **ESP32** or **ESP8266** plus a
**DHT11/DHT22** sensor into a self-contained environmental and network
monitoring probe: it reads temperature/humidity, actively probes your
network (gateway, two IP targets, DNS, HTTP/HTTPS), stores bounded history
locally, and serves its own dashboard and REST API — no cloud, no app,
nothing external required.

This is **Phase 1**, plus an early, opt-in slice of Phase 2 — see
[Phase 2: GEN2 integration plan](#phase-2-gen2-integration-plan). With GEN2
publishing disabled (the default), it remains fully independent of
[GEN2 Bullseye](../GEN2BULLSEYE).

## 1. What it does

- **Environmental monitoring** — temperature and humidity via DHT11 or
  DHT22, selectable at runtime, with bounded local history and charts.
- **Network ground probing** — the device itself pings its gateway, two
  configurable IP targets, resolves a DNS domain, and checks an HTTP/HTTPS
  endpoint, tracking reachability, latency, and packet loss for each.
- **Local dashboard** — a dark, responsive multi-page dashboard served from
  the device's own flash, with zero external dependencies (no CDN, no
  Google Fonts, no cloud JS).
- **Local REST API** — `/api/status`, `/api/environment`, `/api/network`,
  `/api/history`, `/api/config`, plus restart/factory-reset/OTA.
- **Wi-Fi provisioning** — first boot (or factory reset) opens a
  `ENVPROBE-XXXXXX` setup AP with a captive-portal-style page.
- **OTA firmware updates** — upload a new `.bin` from the Settings page.
- **Works with no Internet at all** — the dashboard, API, and environmental
  monitoring all function on a LAN with no upstream connectivity.

## 2. Supported hardware

| Platform | Boards | PlatformIO env |
|---|---|---|
| ESP32 | ESP32 DevKit V1, ESP32-WROOM-32 and compatible boards | `esp32` |
| ESP8266 | NodeMCU, Wemos D1 Mini, ESP8266MOD boards | `esp8266` |

One codebase, two `platformio.ini` environments — see
[docs/hardware.md](docs/hardware.md) for wiring and pin details, and
[docs/architecture.md](docs/architecture.md) for how the hardware
abstraction layer keeps the platform differences contained.

## 3. Quick start

### 3.1 Install PlatformIO

```bash
pip install --user platformio
# or: brew install platformio (macOS), or the PlatformIO IDE VS Code extension
pio --version
```

### 3.2 Wire the sensor

**ESP32 + DHT22** (or DHT11):

```
DHT DATA → GPIO4      (3.3V logic; add a 4.7–10kΩ pull-up between DATA and VCC
DHT VCC  → 3.3V         if your module doesn't already have one on-board)
DHT GND  → GND
```

**ESP8266 (NodeMCU) + DHT22/DHT11**:

```
DHT DATA → D2 (GPIO4)
DHT VCC  → 3.3V
DHT GND  → GND
```

Full wiring notes, including why these GPIOs were chosen and which ones to
avoid, are in [docs/hardware.md](docs/hardware.md).

### 3.3 Build

```bash
pio run -e esp32
pio run -e esp8266
```

### 3.4 Flash firmware + dashboard

```bash
# ESP32
pio run -e esp32 -t upload
pio run -e esp32 -t uploadfs      # uploads data/ (the dashboard) to LittleFS

# ESP8266
pio run -e esp8266 -t upload
pio run -e esp8266 -t uploadfs
```

Flash the filesystem (`uploadfs`) at least once, and again whenever you
change anything under `data/` — firmware and filesystem are uploaded
independently.

### 3.5 First boot — Wi-Fi provisioning

On first boot (or after a factory reset), the device has no Wi-Fi
configured, so it opens its own access point:

```
ENVPROBE-A1B2C3
```

1. Connect a phone or laptop to that Wi-Fi network.
2. A setup page should open automatically (captive portal); if not, browse
   to `http://192.168.4.1`.
3. Note the **auto-generated dashboard username/password** shown on that
   page — you'll need it later to change settings. It's only ever shown
   here, while the device is in setup mode.
4. Enter your Wi-Fi SSID/password, a device name, and pick DHT11 or DHT22.
   Network probe targets have sensible defaults under "Advanced".
5. Submit. The device joins your network and reboots the AP off.

### 3.6 Access the dashboard

Once connected to your Wi-Fi, find the device via:

- **mDNS**: `http://<device-name>.local` (e.g. `http://server-room.local`),
  or the default `http://envprobe.local`
- **Your router's DHCP client list**, or
- **The device's IP directly** (shown in your router, or on the device's
  serial console at boot, 115200 baud)

The live dashboard (temperature, humidity, network health) is visible
without logging in. Changing **Settings**, uploading **OTA firmware**, and
**Factory Reset** require the username/password from step 3.5 (or whatever
you've since changed it to).

## 4. Network probe configuration

From **Settings → Network Monitoring** you can change:

- Gateway target (blank = auto-detect from DHCP)
- Two IP ping targets (default `8.8.8.8`, `1.1.1.1`)
- DNS domain to resolve (default `google.com`)
- HTTP/HTTPS URL to check (default `https://example.com`)
- Probe interval and packet count per probe

See [docs/configuration.md](docs/configuration.md) for the full config
schema and [docs/api.md](docs/api.md) for how to change it via the REST API.

## 5. Historical data

Environment history is sampled at a fixed 60-second resolution into a
bounded, flash-backed ring buffer — old points are overwritten automatically,
so history can never grow without bound. Capacity is sized per platform:

- **ESP32**: 10,080 records ≈ 7 days
- **ESP8266**: 2,880 records ≈ 48 hours (smaller flash budget)

The dashboard's time-range picker (1h/6h/24h/7d) shows as much as is
actually retained — it never claims data that was never stored.

## 6. OTA firmware updates

**Settings → Firmware → Upload Firmware**, choose a `.bin` built for the
same platform (`pio run -e esp32` produces `.pio/build/esp32/firmware.bin`,
likewise for `esp8266`), and upload. The device validates the image via the
platform's own `Update` library (which rejects an image with an invalid
header for that chip) and reboots into it once the upload completes.

## 7. Factory reset

**Settings → Danger Zone → Factory Reset** (requires login), or hold the
factory-reset button for 5 seconds (see [docs/hardware.md](docs/hardware.md)
for which GPIO). Both wipe Wi-Fi credentials, all settings, and history, and
reboot back into provisioning mode.

## 8. REST API

See [docs/api.md](docs/api.md) for the full reference. Summary:

```
GET  /api/status        GET  /api/history?range=1h|6h|24h|7d
GET  /api/environment    GET  /api/config        (auth required)
GET  /api/network        POST /api/config        (auth required)
                          POST /api/restart        (auth required)
                          POST /api/factory-reset  (auth required)
                          POST /api/ota            (auth required)
```

## 9. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Dashboard shows "Dashboard files missing" | You forgot `pio run -e <env> -t uploadfs` |
| Sensor status shows `SENSOR_ERROR` | Check wiring, pull-up resistor, and GPIO in Settings; the device keeps running and serving the dashboard regardless |
| Can't reach `envprobe.local` | Try the IP address directly — not every OS/router resolves mDNS by default |
| Device fell back to the `ENVPROBE-XXXXXX` AP | It couldn't join the configured Wi-Fi after the configured attempt count — check the SSID/password in Settings once reconnected, or re-provision |
| Settings/OTA/Factory Reset return 401 | Use the dashboard username/password from first-boot provisioning |
| `pio run` fails to resolve a library | Check your Internet connection — PlatformIO fetches `lib_deps` from its registry on first build |

## 10. Architecture

See [docs/architecture.md](docs/architecture.md) for the full module map,
the hardware abstraction layer, the non-blocking design, and the memory
budget approach that keeps this working on ESP8266's much smaller RAM/flash.

## 11. Phase 2: GEN2 integration plan

Phase 1 shipped with **no GEN2 Bullseye dependency** — no GEN2 API calls,
auth, cloud telemetry, alerts, or remote configuration. What it had, on
purpose, was a clean seam for Phase 2 to plug into without touching Phase
1's code: `src/telemetry/TelemetryProvider.h`, normalized data models
(`EnvironmentReading`, `NetworkProbeResult`, `DeviceStatus`), and a REST
API/config schema that already looked like what a telemetry adapter would
consume.

That seam has since been filled in, additively, for environment data:
`Gen2Telemetry` (`src/telemetry/Gen2Telemetry.h/.cpp`) is a second
`TelemetryProvider` implementation that POSTs temperature/humidity readings
to a GEN2 Bullseye host's `/api/groundprobe` endpoint, alongside the
existing `LocalTelemetry`. The host is configurable (`gen2ServerUrl`,
default `https://gen2bullseye.com`) — the firmware always appends the fixed
`/api/groundprobe` path itself. It is **opt-in and disabled by default**
(`gen2Enabled = false` in config) — enable it from **Settings → GEN2
Bullseye Integration** with an Org ID and License Key from your GEN2
dashboard (Onboarding tab). See [docs/configuration.md](docs/configuration.md)
for the full field list.

**Note on GEN2's dashboard**: `temperature`/`humidity` are accepted and
stored by GEN2's `/groundprobe` endpoint, but only render on that specific
monitor's own detail/history page — not the general monitor list/card
view, which doesn't show them regardless. See
[docs/configuration.md](docs/configuration.md) for details, including the
TLS tradeoff `Gen2Telemetry` makes (`setInsecure()` — see that doc and
`src/telemetry/Gen2Telemetry.h` for why).

Still out of scope: network-probe publishing, device-status/registration
publishing, remote config, and cloud alerts sourced from GEN2 rather than
this device's own local thresholds.

## Project layout

```
esp-environment-probe/
├── platformio.ini
├── src/            firmware source (see docs/architecture.md)
├── data/            dashboard (served from LittleFS)
├── docs/            architecture, hardware, api, configuration, development
└── test/
```

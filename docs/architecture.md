# Architecture

## Module map

```
main.cpp                     composition root: owns every manager, wires
                              them together, runs the non-blocking loop()

config/ConfigManager         versioned JSON config (Wi-Fi, sensor, probes,
                              thresholds, auth) — the only thing that reads/
                              writes config.json

environment/
  EnvironmentSensor           abstract interface (begin/read/getSensorType)
  DHTSensor                   concrete impl — DHT11 or DHT22, chosen at
                               runtime, wrapping Adafruit's DHT library
  EnvironmentManager           schedules reads, tracks current reading +
                               status, owns the bounded history ring

network/
  NetworkManager                Wi-Fi STA lifecycle, reconnect/backoff,
                                 provisioning AP + captive DNS, mDNS
  NetworkProbe                  gateway/ping/DNS/HTTP ground probes

storage/StorageManager        LittleFS wrapper: JSON file read/write,
                              atomic writes (temp file + rename), wipe-all

device/DeviceManager          device ID, boot count, first-boot password
                              generation, DeviceStatus snapshot

web/WebServerManager          dashboard + REST API + auth + OTA upload
                              routing (synchronous WebServer)

ota/OTAManager                thin wrapper around the Update library

telemetry/
  TelemetryProvider              Phase 2 seam (see below)
  LocalTelemetry                 Phase 1's only implementation

hardware/                    hardware abstraction layer (HAL)
  Platform.h / PlatformManager.cpp   chip info, MAC, reset reason, watchdog
  HardwareConfig.h                    per-platform pin/constant defaults
  FSCompat.h                          LittleFS.begin() differences
  PingCompat.h                        ESP32Ping vs ESP8266Ping differences

util/
  Logger                        lightweight Serial + in-RAM ring logging
  CircularLog                   generic bounded flash ring buffer
```

Every manager depends only on the headers it actually needs (mostly
`ConfigManager` and each other's narrow public interface) — nothing outside
`hardware/` contains a bare `#ifdef PLATFORM_ESP32` / `#ifdef PLATFORM_ESP8266`
except where a library's *name* differs across platforms (e.g.
`ESP32Ping.h` vs `ESP8266Ping.h`, `Update.h` vs `Updater.h`), which is
exactly what the HAL exists to absorb.

## Why a synchronous WebServer, not an async library

`ESPAsyncWebServer` + `AsyncTCP`/`ESPAsyncTCP` is the more commonly
recommended stack for ESP web servers, but it pulls in more registry
dependencies with more cross-version compatibility risk across ESP32 *and*
ESP8266 at once (two different underlying TCP backends). Phase 1 uses the
synchronous `WebServer`/`ESP8266WebServer` that ship with each core instead:
same header shape on both platforms (`server.on(...)`, `HTTPUpload`,
`streamFile`), one dependency graph, nothing extra to break. The trade-off
is handled by discipline, not a library: every handler does bounded, quick
work (see below).

## Non-blocking design

`loop()` never calls `delay()` for anything periodic. Every manager is
driven by its own `millis()`-based interval check inside `loop()`:

- `NetworkManager::loop()` — checks Wi-Fi status, backs off reconnect
  attempts to once per 5s, processes the captive-portal DNS server.
- `EnvironmentManager::loop()` — reads the sensor at most once per
  `environmentIntervalS`.
- `NetworkProbe::loop()` — round-robins through gateway/ping1/ping2/dns/http,
  running **at most one probe per `loop()` call**.
- `WebServerManager::loop()` — `handleClient()`, which itself only does work
  when a request is actually pending.

One deliberate, documented exception: **individual network probes are
short, bounded blocking calls** (ping, DNS resolution, an HTTP GET), because
none of the Arduino/lwIP APIs for these on either platform offer a fully
async surface without adding an async networking library. The mitigation is
architectural, not a `delay()`: `NetworkProbe::loop()` runs *one* probe per
call and returns, so a stall is bounded to that single probe's timeout
(a low number of seconds at worst) rather than all probes serialized
back-to-back — and the web server, sensor loop, and Wi-Fi maintenance all
still get a turn between probes.

The one intentional blocking call outside `loop()` is the **first** Wi-Fi
connection attempt in `NetworkManager::begin()` — during `setup()`, before
the web server or anything else is running, where blocking briefly to know
whether to fall back to provisioning mode is the correct behavior.

## Memory budget (why ESP8266 works too)

ESP8266 has ~80KB RAM total vs. ESP32's ~320KB, and roughly half the app
flash of the larger of ESP32's two dual-OTA partitions. The choices that
keep both platforms comfortably under budget:

- **History is a fixed-size binary ring file**, not JSON-on-flash — O(1)
  append, and the file can never grow past
  `header + recordSize × maxRecords` bytes. Capacity is smaller on ESP8266
  (2,880 records / 48h) than ESP32 (10,080 / 7 days) for exactly this
  reason. See `util/CircularLog`.
- **`/api/history` is hand-written, streamed JSON**, not built as one large
  `ArduinoJson` document — `CircularLog::readRecentDownsampled()` seeks
  directly to the ≤300 records the response needs (evenly strided across
  whatever range was requested) and `WebServerManager` writes each one as a
  small `snprintf`'d chunk via `sendContent()`. A 7-day request on ESP32
  costs the same handful of KB and ~300 small flash reads as a 1-hour
  request — never "read everything into RAM and filter."
- **The ESP32 build uses `min_spiffs.csv`**, trading unused LittleFS space
  (the dashboard is a few dozen KB) for a larger dual-OTA app partition
  (~1.9MB per slot instead of ~1.3MB), since OTA support needs headroom for
  a full second firmware image.
- Static buffers (e.g. the `/api/history` output buffer) are declared
  `static`, not on the stack, to avoid a multi-KB stack allocation deep
  inside the web server's call chain on ESP8266's smaller task stack.
- `WiFiClientSecure`/BearSSL for the HTTPS probe is the single heaviest
  dependency pulled in; it's used with `setInsecure()` because the probe's
  job is reachability/latency, not certificate trust — see
  [api.md](api.md) for the HTTP/HTTPS probe's exact semantics.

## Auth model

The dashboard's live monitoring views (`/api/status`, `/environment`,
`/network`, `/history`) are unauthenticated — they contain no secrets and
are meant to be glanceable on the LAN. Everything that changes device state
or reveals configuration (`/api/config` GET *and* POST, `/api/restart`,
`/api/factory-reset`, `/api/ota`) requires HTTP Basic Auth against
`authUsername`/`authPassword` in config. `GET /api/config` redacts
`wifiPassword` and `authPassword` even though the request is authenticated,
so the current values are never round-tripped back to the browser.

A dashboard password is auto-generated on first boot rather than shipping
one fixed default, and is only ever readable, unauthenticated, from
`/api/provisioning-info` — which itself only answers while the device is
still in its own open provisioning AP (i.e. before it has joined a real
network). See `docs/api.md`.

## Phase 2 boundary

`src/telemetry/TelemetryProvider.h` is the seam Phase 2 (GEN2 Bullseye
integration) is meant to fill. Phase 1 shipped one implementation,
`LocalTelemetry`, which is close to a no-op: `EnvironmentManager` and
`NetworkProbe` already persist/hold their own data directly, so there's
nothing further for Phase 1's "publish" step to do. `TelemetryProvider`
exists so a second implementation — `Gen2Telemetry`, covering GEN2
environment publishing — can be added (and now has been, for environment
data; network/device-status publishing remain unimplemented, see
`Gen2Telemetry`'s own header) without modifying environment, network,
storage, or dashboard code.

Phase 1 shipped with nothing importing, linking against, or calling out to
any GEN2 endpoint, by design. That boundary has since been deliberately,
explicitly crossed once: `src/telemetry/Gen2Telemetry.h/.cpp` is a second
`TelemetryProvider` implementation that POSTs environment (temperature/
humidity) readings to GEN2 Bullseye's live `groundprobe` endpoint. It is
opt-in and disabled by default (`gen2Enabled = false`) — a device with
default config still makes zero GEN2 calls, unchanged. This is not a
Phase 1 architectural violation; it's Phase 2's first increment, added
early and additively (a second `TelemetryProvider` alongside
`LocalTelemetry`, per the seam above) rather than as a wholesale Phase 2
cutover — see `docs/configuration.md`'s GEN2 fields for what it does and
doesn't send, and note today that GEN2's own backend doesn't yet persist
or display the temperature/humidity fields this sends (a known,
intentional gap on GEN2's side, not this firmware's).

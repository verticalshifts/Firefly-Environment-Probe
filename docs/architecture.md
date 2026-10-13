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
ota/BootGuard                 boot-confirmation flag + ESP32 auto-revert
ota/OTAUpdateChecker          opt-in, notify-only GitHub Releases polling
ota/GitHubApiRootCA.h         pinned root for api.github.com (Sectigo, ECDSA)
ota/GitHubAssetRootCA.h       pinned root for the release-asset CDN (Let's Encrypt)

telemetry/
  TelemetryProvider              Phase 2 seam (see below)
  LocalTelemetry                 Phase 1's only implementation

hardware/                    hardware abstraction layer (HAL)
  Platform.h / PlatformManager.cpp   chip info, MAC, reset reason, watchdog
  HardwareConfig.h                    per-platform pin/constant defaults
  FSCompat.h                          LittleFS.begin() differences
  PingCompat.h                        ESP32Ping vs ESP8266Ping differences
  NetworkHealthIndicator               non-blocking ping-latency LED driver (8.8.8.8)

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
rather than all probes serialized back-to-back within one call — and the
web server, sensor loop, and Wi-Fi maintenance do get a turn between
probes.

**In practice that turn can be too brief to matter**, though: Gateway,
Probe Target 1, and Probe Target 2 all share `networkIntervalS`, so they
become "due" at the same wall-clock moment and — since each one's own
blocking duration is what makes the *next* one's turn arrive — end up
running essentially back-to-back anyway, with the intervening loop()
passes taking negligible real time by comparison. Confirmed live: at
`probePacketCount = 5` (ping's standard ~1s/packet pacing), that's ~5s
*each*, ~15s combined, recurring every `networkIntervalS` — long enough to
starve WiFi servicing and cause real packet loss to the device itself, not
just a probe UI number. `probePacketCount` is the practical lever on this
(default lowered to `1` — ~1s per probe, ~3s combined — for exactly this
reason); see [configuration.md](configuration.md).

The one intentional blocking call outside `loop()` is the **first** Wi-Fi
connection attempt in `NetworkManager::begin()` — during `setup()`, before
the web server or anything else is running, where blocking briefly to know
whether to fall back to provisioning mode is the correct behavior.

**A second named exception**: `POST /api/ota/install-latest`
(`WebServerManager::handleApiOtaInstallLatest()`) downloads and flashes the
firmware update synchronously, inside the request handler, blocking the
whole (single-threaded) `WebServer` for the download's full duration —
deliberately, not an oversight. The existing manual-upload OTA path
already blocks the same way for its entire duration (pre-existing
precedent this doesn't introduce); building a genuinely non-blocking
chunked downloader (a live `HTTPClient`+`SecureClient` stream kept open
across multiple `loop()` passes, correctly interleaved with
`Update.write()`) is real, failure-prone complexity for a rare,
deliberately human-triggered, one-off action — not the kind of routine
continuous work (`NetworkProbe`, `EnvironmentManager`) the non-blocking
mandate above targets. `yield()`/`PlatformManager::feedWatchdog()` is
called per chunk so the platform watchdog doesn't trip during the transfer.

## History timestamps survive reboots

`EnvironmentReading`/`EnvHistoryPoint` timestamps come from
`DeviceManager::getContinuousUptimeS()`, not plain `millis()/1000`. Neither
platform has a battery-backed RTC, so `millis()` resets to 0 on every
reboot — but the environment history *file* lives on LittleFS and survives
reboots. Stamping points with plain `millis()/1000` meant every reboot
reset the clock those points are measured against while old points from
before the reboot stayed in the ring, so the file ended up with wildly
non-monotonic timestamps interleaved together — confirmed live as the
cause of the dashboard's history charts rendering as scrambled/crossed
lines after this device's many reboots during development.
`getContinuousUptimeS()` adds a persisted offset (`DeviceManager::loop()`
saves it to `/device.json` every 5 minutes) on top of the current
session's own `millis()/1000`, so the clock only ever moves forward across
a reboot — accurate to within one save interval of an ungraceful reset,
which is a fine trade for a monitoring history feature. `js/charts.js`
also sorts points by timestamp defensively before drawing, so old
already-corrupted history (from before this fix, still aging out of the
ring) can't render as a broken chart either.

Session-relative uptime (`DeviceManager::getUptimeSeconds()`, still plain
`millis()/1000`) is unchanged and used deliberately where a reboot
*resetting* the value is the actual signal — e.g. the OTA install flow's
Settings-page polling detects a successful reboot by watching
`uptimeSeconds` drop.

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
- `Gen2Telemetry` also uses `setInsecure()`, for a different reason: this
  chip's software TLS stack verifying an RSA-4096 chain took ~12s of
  CPU-bound work, long enough to cause real WiFi packet loss elsewhere in
  the firmware while it ran. See `src/telemetry/Gen2Telemetry.h`'s header
  comment for the tradeoff this accepts (the secret `gen2LicenseKey` is no
  longer protected against a network-path MITM).

## Auth model

The dashboard's live monitoring views (`/api/status`, `/environment`,
`/network`, `/history`, `/api/ota/status`) are unauthenticated — they
contain no secrets and are meant to be glanceable on the LAN. Everything
that changes device state or reveals configuration (`/api/config` GET
*and* POST, `/api/restart`, `/api/factory-reset`, `/api/ota`,
`/api/ota/check-now`, `/api/ota/install-latest`) requires HTTP Basic Auth
against `authUsername`/`authPassword` in config. `GET /api/config` redacts
`wifiPassword`, `authPassword`, and `wifiEapPassword` even though the
request is authenticated, so the current values are never round-tripped
back to the browser. `wifiUsername` (the WPA2-Enterprise identity/username)
is *not* redacted — like `authUsername`, it isn't a secret.

**WPA2-Enterprise trade-offs** (see `NetworkManager::performConnectAttempt()`):
neither platform validates the RADIUS/authentication server's certificate —
ESP32's `WiFi.begin(..., WPA2_AUTH_PEAP, ...)` call passes `ca_pem=NULL`,
and the ESP8266 path (below) has no certificate plumbing wired up at all —
so the device trusts any AP/server claiming the configured SSID, the same
class of MITM trade-off already accepted for `gen2LicenseKey` above. Only
PEAP/MSCHAPv2 is supported, not EAP-TLS/TTLS or WPA3-Enterprise. On ESP8266
specifically, Enterprise auth is implemented via raw NONOS SDK C calls
(`wifi_station_set_wpa2_enterprise_*`) rather than any Arduino-supported
API — the Arduino-ESP8266 core has no Enterprise wrapper at all, unlike
ESP32's `esp_wpa2.h`. This is a known community pattern, not an
Espressif-supported one, and should be re-verified on real hardware after
any ESP8266 platform/core version bump in `platformio.ini`.

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
humidity) readings to a GEN2 Bullseye host's `/api/groundprobe` endpoint —
the host is configurable (`gen2ServerUrl`, default
`https://gen2bullseye.com`), the path is fixed. It is
opt-in and disabled by default (`gen2Enabled = false`) — a device with
default config still makes zero GEN2 calls, unchanged. This is not a
Phase 1 architectural violation; it's Phase 2's first increment, added
early and additively (a second `TelemetryProvider` alongside
`LocalTelemetry`, per the seam above) rather than as a wholesale Phase 2
cutover — see `docs/configuration.md`'s GEN2 fields for what it does and
doesn't send. GEN2's backend does persist `temperature`/`humidity`, but
only renders them on a monitor's own detail/history page, not the general
monitor list — see `docs/configuration.md`'s GEN2 section.

A third provider, `src/telemetry/IotGatewayTelemetry.h/.cpp`, adds a
secondary path to a Firefly-CAP100 IoT Gateway on the LAN
(`POST <iotgwUrl>` with a bearer token), which buffers and forwards to GEN2
itself. It runs alongside `Gen2Telemetry`, not instead of it; both are
independently opt-in (`iotgwEnabled`, `gen2Enabled`), and the gateway token
is redacted from `GET /api/config` like the other secrets.

## Network recovery

Three things keep an outage from becoming a permanent outage. All were added
after a device failed to come back online following an internet interruption.

**The provisioning AP is not a dead end.** `begin()` falls back to the setup
AP after ~60s of failed connect attempts, which is right for a genuinely
misconfigured device — but `loop()` used to `return` immediately in AP mode,
so nothing ever retried the stored network again. A device that rebooted
during an outage sat in setup mode forever, even once the network returned.
`NetworkManager::retryConfiguredNetworkFromAP()` now retries every 60s, using
one bounded `performConnectAttempt()` rather than the full ~60s budget
(this runs inside `loop()` with the watchdog armed, unlike the boot path). It
skips the retry entirely while a client is associated with the AP, so it
can't yank the portal away from someone mid-provisioning. Each retry feeds
the watchdog at its bounded stages: a scan was measured at ~6.3s on a band
with 30 networks, which with the 8s join wait is close to the 15s limit.

**One owner for reconnection, and attempts get time to finish.** `loop()`
used to call `WiFi.reconnect()` every 5s. Each call tears down the attempt in
progress, and a join plus DHCP can take longer than 5s on a weak or busy
link, so the device could cancel its own attempts indefinitely; the core's
auto-reconnect was also left on, issuing connects of its own. Now the core's
auto-reconnect is off, the first response to a drop is a plain `reconnect()`
(right for a blip), and every later attempt — 15s apart — is a full one that
is started and then left to run.

**A scan needs the station idle.** `scanNetworks()` returns `-2`
(`WIFI_SCAN_FAILED`) while a connect is pending. Every attempt after the
first, and every re-scan made from `loop()`, hit that, so the "re-scan and
target the strongest AP" path never actually scanned. The pending connect is
now stopped first.

**Hidden SSIDs skip the pointless scan.** A hidden network can be joined by
name but never appears in a scan, so it can't be ranked or targeted. Once two
scans in a row have not listed the configured SSID, the ~6s app-level scan is
skipped (re-checked every 5th attempt). Switching the driver to
`WIFI_ALL_CHANNEL_SCAN` sorted by signal was tried for this case and removed:
a test on a hidden-SSID network showed no benefit.

**Wi-Fi power-save is off** (`WiFi.setSleep(false)` / `WIFI_NONE_SLEEP`).
The device is USB-powered, and modem-sleep is a known source of dropped
links with some access points. This is a precaution: no drop observed so far
has been traced to it.

**Disconnect reasons are recorded.** The stack only reports why a link
dropped through an event. `NetworkManager` now captures the reason code, logs
it with a plain-language reading, and exposes it as
`wifi.lastDisconnectReason` / `wifi.lastDisconnectText` in `/api/network`.
Reason 8 (our own disconnect-before-scan) is ignored so it cannot overwrite
the reason that matters.

**Transmit power is capped at 15dBm by default (ESP32)** and is a per-device
setting, `wifiTxPowerDbm`, applied live. A minute-by-minute A/B on a bench
board showed no link-quality difference between 7 and 18dBm, so this is a
knob for boards with a weak supply or a long reach, not a fix for drops. The
radio rounds down to the step it supports; the live value is `wifi.txPowerDbm`
in `/api/network`.

**Outbound publishes are time-bounded (ESP32).** `HTTPClient::setTimeout()`
covers reads only, so the TCP connect and TLS handshake of a GEN2, monitor-sync
or IoT Gateway request were unbounded; on a degraded link one publish was
measured blocking `loop()` — and the web UI with it — for 93s. They are now
limited by `hw::HTTP_CONNECT_TIMEOUT_MS` and `hw::TLS_HANDSHAKE_TIMEOUT_S`.

**Last resort: restart after 10 minutes offline.** Observed live: a device
that had failed every reconnect for over a minute joined the same AP at once
after a power cycle. If station mode has been down for `STUCK_RESTART_MS`
despite the attempts above, the device restarts. This cannot loop: a restart
during a real outage ends in the provisioning AP, which keeps retrying on its
own timer.

**The watchdog is fed between blocking steps.** `feedWatchdog()` used to run
only at the top of `loop()`. During an outage the GEN2 POST, monitor beacons,
sync poll and HTTPS probe all sit in their timeouts in the *same* iteration,
which could exceed the 15s watchdog and reset the device — dropping it into
the AP dead-end above. `main.cpp`'s `serviceWeb()` now feeds the watchdog at
the same points it services the web server. The watchdog still catches a
genuine hang inside any single step.

## OTA rollback safety (ESP32 vs. ESP8266)

Both OTA paths (manual upload, `POST /api/ota/install-latest`) arm
`BootGuard` (`src/ota/BootGuard.h/.cpp`) right before restarting into the
new image. The new firmware must prove itself stable — Wi-Fi connected
continuously for 15s, the cheapest meaningful signal available without new
instrumentation, and every real regression found live this session was a
WiFi-stability problem, not sensor/logic — within a 2-minute window (or
across 3 boot-crash cycles, if it never reaches `loop()` at all) or the
update is treated as failed.

**ESP32 gets a real automatic revert.** `armPendingConfirm()` captures the
currently-running (known-good) partition's label via
`esp_ota_get_running_partition()`; a failed confirmation calls
`esp_ota_set_boot_partition()` back to it and restarts. This works via
Arduino-ESP32's exposed `esp_ota_ops.h`/`esp_partition.h` APIs — stable,
public, documented — without needing the bootloader's native
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` feature, which isn't compiled into
the framework's prebuilt bootloader by default.

**ESP8266 gets report-only: no automatic revert is attempted.** This is
deliberate, not an oversight. There is no equivalently stable, documented
Arduino-core API for it — only low-level, undocumented-for-this-purpose
`eboot_command` bootloader manipulation, which risks bricking the device if
done wrong, for a codebase with zero prior OTA-rollback track record
before this feature. A failed confirmation on ESP8266 just persists
`lastFailedToConfirm=true` (visible via `GET /api/ota/status`, survives a
power cycle) and logs clearly — the device keeps running whatever it
booted into, and a human has to intervene (re-flash over serial, or
another OTA) if that firmware turns out to be broken.

**Known, disclosed gap on both platforms**: if a bad image crashes hard
enough that `loop()` never runs even once, only the flash-persisted
attempt counter (not the in-RAM confirm timer) catches it — and only after
3 full boot-crash cycles, not immediately.

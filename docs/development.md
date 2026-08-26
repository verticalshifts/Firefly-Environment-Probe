# Development

## Toolchain

```bash
pip install --user platformio   # or pipx install platformio (recommended by upstream)
pio --version
```

This repo was developed and verified against PlatformIO Core 6.1.x,
`espressif32` platform 7.x (arduino-esp32 core ~2.0.17), and `espressif8266`
platform 4.2.x (ESP8266 Arduino core ~3.1.2).

## Building

```bash
pio run -e esp32       # or: pio run  (builds every env in platformio.ini)
pio run -e esp8266
```

## Running the unit tests

```bash
pio test -e native
```

Runs `test/test_ringmath` — 10 host-side tests against
`src/util/RingMath.h`, the pure ring-buffer index arithmetic behind
`CircularLog`. No board required; this is the one piece of Phase 1's logic
that's decoupled enough from Arduino/FS to test off-device. See
[test/README.md](../test/README.md) for what is (and deliberately isn't)
covered this way, and `native`'s `[env:native]` block in `platformio.ini`
for why it overrides every `[env]` default (it must not pull in the Arduino
framework or ESP-only `lib_deps`).

## Filesystem image (dashboard)

The dashboard lives in `data/` and is packaged into a separate LittleFS
image from the firmware itself:

```bash
pio run -e esp32 -t buildfs      # build the image without flashing
pio run -e esp32 -t uploadfs     # build + flash it to a connected device
```

Re-run `uploadfs` any time you change anything under `data/` — it is not
part of the firmware binary and `-t upload` does not touch it.

## Flashing

```bash
pio run -e esp32 -t upload -t uploadfs
pio run -e esp8266 -t upload -t uploadfs
```

`pio device monitor -e esp32` (or `esp8266`) opens the serial console at
115200 baud — this is where `Logger` output (section on logging below)
goes.

## Project structure

See [architecture.md](architecture.md) for the module map. In short:
each subsystem under `src/` owns one concern and is tested by compiling —
there's no hardware-in-the-loop test harness in Phase 1 (see "What isn't
tested here" below).

## Adding a config field

1. Add the field (with a sensible default) to `DeviceConfig` in
   `src/config/ConfigManager.h`.
2. Read/write it in `ConfigManager::fromJson()` / `toJson()` in the `.cpp`.
3. Accept it in `ConfigManager::update()` if it should be settable via the
   API/Settings page, with validation in `validate()` if it has a valid
   range.
4. Add the corresponding `<input>`/`<select>` to `data/settings.html` (and
   `data/provision.html` if it's relevant during first-boot setup) — the
   field `name` attribute must match the JSON key exactly, since
   `settings.js` maps form fields to JSON fields generically by name.
5. Document it in [configuration.md](configuration.md).

## Adding a REST endpoint

Add a route in `WebServerManager::setupRoutes()` and a handler method,
following the existing handlers as a template. Decide whether it needs
`requireAuth()` — see "Auth model" in [architecture.md](architecture.md)
for the read-only-vs-mutating split Phase 1 uses.

## Coding conventions

- **Non-blocking `loop()`**: no `delay()` for anything periodic — use a
  `millis()`-based interval check, following the pattern in
  `EnvironmentManager::loop()` / `NetworkProbe::loop()`.
- **HAL boundary**: platform differences (`#ifdef PLATFORM_ESP32` /
  `PLATFORM_ESP8266`) belong in `src/hardware/`, not scattered through
  `src/environment/`, `src/network/`, etc. If you need a new
  platform-specific call, add a small wrapper there first.
- **Memory discipline on the hot paths**: avoid building large
  `ArduinoJson` documents for anything that scales with history/records —
  see `WebServerManager::handleApiHistory()` for the pattern (hand-built,
  streamed JSON, static buffers, bounded output size).
- **Logging**: use `Logger::info/warn/error(tag, message)`, not raw
  `Serial.print` — it's cheap (no flash writes) and keeps a small in-RAM
  ring for future diagnostics use.

## What isn't tested here (and why)

Phase 1 was built and verified by compiling both PlatformIO environments to
completion (`pio run -e esp32` / `-e esp8266`, both `SUCCESS`) and building
the LittleFS filesystem image for both. The one piece of actual logic that
gets automatically tested is `src/util/RingMath.h` via `pio test -e native`
(see above) — everything else is compiled but not exercised. It has **not**
been flash-verified on physical hardware as part of this delivery — there
was no ESP32/ESP8266 board attached to build this on. Before relying on it:

1. Flash both a real ESP32 and ESP8266 board and run through
   [README.md](../README.md)'s Quick Start end-to-end.
2. Work through the manual test matrix below — sensor faults, Wi-Fi
   failure/recovery, provisioning fallback, OTA success/failure, factory
   reset, and a 24+ hour soak checking `freeHeap` doesn't trend downward
   (a slow leak only shows up over time, not in a compile check).

### Manual test matrix (section 39 of the original spec)

| Area | Cases to run by hand |
|---|---|
| DHT | DHT11, DHT22, sensor disconnected mid-run, sensor reconnected |
| Wi-Fi | correct/incorrect credentials, AP unavailable, disconnect + auto-reconnect, provisioning AP → join flow |
| Network | gateway up/down, internet up/down, DNS success/failure, induced packet loss, high latency |
| Storage | config persists across reboot, history survives reboot, ring wraps correctly once full, factory reset actually clears everything |
| Dashboard | desktop + mobile layout, all five pages, chart range switching, Settings save round-trip, auth prompt on protected endpoints |
| OTA | successful update (device boots new firmware), an intentionally corrupt/wrong-platform `.bin` (device reports the error and keeps running the old firmware) |
| Stability | ≥24h continuous run: no crash, no reset-reason other than the ones you triggered, `freeHeap` stays flat, Wi-Fi and sensor recover from an induced fault without a manual restart |

None of this is optional before treating Phase 1 as production-ready —
compiling clean proves the code is well-formed, not that a real DHT22 on a
real GPIO behaves the way the code assumes under real conditions.

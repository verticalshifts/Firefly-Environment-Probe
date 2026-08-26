# Tests

Phase 1 does not include an automated hardware-in-the-loop test suite —
most of what needs verifying (DHT sensor behavior, real Wi-Fi
join/reconnect, actual network reachability, OTA against a real bootloader,
24-hour stability) genuinely requires physical ESP32/ESP8266 boards and
can't be meaningfully faked in a PlatformIO `native` unit test.

What "tested" means for this delivery: both PlatformIO environments compile
cleanly (`pio run -e esp32` and `pio run -e esp8266`, both `SUCCESS`,
verified as part of building this codebase) and the LittleFS filesystem
image builds successfully for both (`pio run -e <env> -t buildfs`).

**Before treating this as production-ready**, work through the manual test
matrix in [docs/development.md](../docs/development.md#what-isnt-tested-here-and-why)
on real hardware — it covers sensor faults, Wi-Fi failure/recovery,
provisioning, network probe edge cases, storage/factory-reset, the
dashboard, OTA, and a 24+ hour stability soak.

If you add a PlatformIO `native` test environment later for pure-logic
pieces (e.g. `CircularLog`'s ring-buffer math, `ConfigManager` validation),
this is where it belongs — see PlatformIO's [Unit Testing
docs](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html).

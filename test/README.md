# Tests

## Automated: `test_ringmath` (native, no board required)

```bash
pio test -e native
```

10 unit tests against `src/util/RingMath.h` — the pure index arithmetic
behind `CircularLog`'s ring buffer (append/wrap behavior, logical→physical
slot mapping, and the `/api/history` downsampling plan), extracted into a
header with zero Arduino/FS dependency specifically so it's testable on the
host. This is the one piece of Phase 1's logic that's genuinely pure enough
to unit test; everything else below still needs real hardware. All 10 pass
as of this delivery.

`native` is intentionally not in `platformio.ini`'s `default_envs` — it
never gets pulled into a plain `pio run` or `pio run -e esp32`/`esp8266`.

## Everything else: manual, on real hardware

Phase 1 does not include an automated hardware-in-the-loop test suite for
the rest of the firmware — most of what needs verifying (DHT sensor
behavior, real Wi-Fi join/reconnect, actual network reachability, OTA
against a real bootloader, 24-hour stability) genuinely requires physical
ESP32/ESP8266 boards and can't be meaningfully faked in a native test.

What "tested" means for the rest of this delivery: both PlatformIO
environments compile cleanly (`pio run -e esp32` and `pio run -e esp8266`,
both `SUCCESS`, verified from a clean `.pio/` cache) and the LittleFS
filesystem image builds successfully for both (`pio run -e <env> -t buildfs`).

**Before treating this as production-ready**, work through the manual test
matrix in [docs/development.md](../docs/development.md#what-isnt-tested-here-and-why)
on real hardware — it covers sensor faults, Wi-Fi failure/recovery,
provisioning, network probe edge cases, storage/factory-reset, the
dashboard, OTA, and a 24+ hour stability soak.

If you extract more pure-logic pieces later (e.g. `ConfigManager`
validation), add a sibling `test/test_<name>/` directory the same way
`test_ringmath` is set up — see PlatformIO's [Unit Testing
docs](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html).

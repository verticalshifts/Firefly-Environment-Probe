# Hardware

## Supported boards

| Platform | Tested/expected boards |
|---|---|
| ESP32 | ESP32 DevKit V1, ESP32-WROOM-32 dev boards (PlatformIO board id `esp32dev`) |
| ESP8266 | NodeMCU 1.0 (ESP-12E), Wemos D1 Mini, generic ESP8266MOD boards (PlatformIO board id `nodemcuv2`) |

Other boards on the same chip family will very likely work by changing the
`board = ...` line in `platformio.ini` — the firmware itself only depends on
the chip family (`PLATFORM_ESP32`/`PLATFORM_ESP8266`), not a specific board.

## DHT wiring

Both DHT11 and DHT22 use the same 3-wire (or 4-wire, with NC) hookup:

```
DHT   VCC  →  3.3V
DHT   GND  →  GND
DHT   DATA →  the GPIO configured below
```

If your DHT module is a bare sensor (not a breakout board), add a
**4.7 kΩ–10 kΩ pull-up resistor between DATA and VCC**. Most breakout
boards (the 3-pin blue PCB modules) already include this pull-up.

### ESP32 + DHT11/DHT22

```
DHT DATA → GPIO4
```

GPIO4 is not a strapping pin on ESP32 and is safe to use from boot.

### ESP8266 (NodeMCU/Wemos) + DHT11/DHT22

```
DHT DATA → D2   (silkscreen "D2" = GPIO4)
```

**Board pin labels vs. GPIO numbers**: NodeMCU/Wemos silkscreen labels
("D0", "D1", "D2", …) do **not** match the underlying GPIO numbers the
firmware (and Arduino core) actually use. `D2` is `GPIO4`, `D1` is `GPIO5`,
`D4` is `GPIO2`, and so on — always use the GPIO number, not the "D" label,
when configuring `sensorGpio` in Settings.

## Physical factory-reset / provisioning button

Hold for 5 seconds to wipe Wi-Fi credentials, settings, and history, and
reboot into provisioning mode (same effect as Settings → Factory Reset).

| Platform | GPIO | Notes |
|---|---|---|
| ESP32 | GPIO0 | Doubles as the BOOT button on most dev boards; already pulled up on-board. Only sampled after `setup()` completes, so it doesn't interfere with the boot-strapping use of this pin. |
| ESP8266 | GPIO0 (silkscreen "D3" on NodeMCU) | Often wired to a "FLASH" button on NodeMCU boards; same boot-strapping caveat and same "only sampled after boot" mitigation. |

If your board has no button wired to this GPIO, factory reset is still
available from **Settings → Danger Zone → Factory Reset** in the dashboard.

## GPIOs to avoid

When changing `sensorGpio` (or wiring your own button) away from the
defaults above:

- **ESP32**: avoid GPIO6–11 (connected to the internal flash), and treat
  GPIO34–39 as input-only (fine for a button, not usable for the DHT data
  line since DHT is bidirectional-ish/open-drain).
- **ESP8266**: avoid GPIO6–11 (flash), GPIO9/GPIO10 on most breakouts
  (often unavailable), and GPIO16 (no interrupt support, different pull-up
  behavior — avoid for the DHT line).

## Onboard status LED

| Platform | GPIO |
|---|---|
| ESP32 (most DevKit boards) | GPIO2 |
| ESP8266 (most boards) | GPIO2 (silkscreen "D4"), active-LOW |

Phase 1 doesn't currently drive this LED from firmware (kept out per
"don't over-engineer" — it's a config constant in `hardware/HardwareConfig.h`
ready for a future status-blink feature, not wired to anything yet).

## Flash partitioning

- **ESP32** uses the `min_spiffs.csv` partition table: two ~1.9MB OTA app
  slots and a 128KB LittleFS partition. The dashboard is a few dozen KB, so
  128KB leaves comfortable headroom; the trade favors OTA/app space, which
  is scarcer.
- **ESP8266** uses the board's default `nodemcuv2` layout: two ~1MB OTA
  app slots and roughly 1MB of LittleFS. Check `pio run -t buildfs` output
  if you need the exact current numbers for your framework version.

Both are set via `board_build.filesystem = littlefs` in `platformio.ini`,
so `pio run -t uploadfs` / `-t buildfs` (or your IDE's equivalent) always
target LittleFS.

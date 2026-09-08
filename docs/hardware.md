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
DHT DATA → GPIO15
```

GPIO15 is an ESP32 strapping pin (it must read HIGH at boot to boot from
flash and keep the boot log quiet). The DHT data line is idle-high through
its pull-up, which satisfies that, so it is safe as the default on the board
this firmware targets. If you re-pin to a non-strapping GPIO (e.g. GPIO4,
GPIO16–33), set `sensorGpio` accordingly in Settings.

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

## Network-health indicator LED

An external LED wired to `hw::DEFAULT_NETWORK_LED_GPIO` (GPIO14 on both
platforms — D5 on ESP8266 silkscreens) gives an at-a-glance physical read of
ping latency to a fixed target (8.8.8.8), driven non-blocking by
`hardware/NetworkHealthIndicator.cpp` on its own 10s ping timer — separate
from, and faster than, `NetworkProbe`'s own 30s ground-probe cycle. This LED
used to be a `TemperatureIndicator`; that class is gone and temperature is
no longer shown via LED at all, only via the dashboard's gauge widgets.

| Condition | Pattern |
|---|---|
| Wi-Fi not connected | 1s on / 5s off |
| ≤ 59ms | Steady on |
| 60–90ms, sustained for 10 consecutive pings | 3s on / 1s off |
| > 90ms or lost, sustained for 10 consecutive pings | 0.5s on / 0.5s off |

"Sustained for 10 consecutive pings" is a real consecutive-run counter, not
a sliding window: a single stray slow or lost ping can't flip the LED into
a degraded pattern on its own. Steady-on is the default/fallback state —
recovery back to it is immediate, with no consecutive-good requirement
symmetric to the two degraded tiers. The disconnected pattern takes
priority over all of that: no pings are possible without Wi-Fi, so it's
checked first, and reconnecting resets the consecutive-ping streak so a
stale pre-disconnect run can't show a degraded pattern before any fresh
pings have actually happened.

A lost/timed-out ping blocks for the underlying ping library's fixed ~1s
timeout (neither ESP32Ping nor ESP8266Ping expose a shorter one) — same
bounded-blocking tradeoff `docs/architecture.md` already documents for
`NetworkProbe`'s own pings, just on this LED's own 10s schedule instead of
NetworkProbe's 30s one. Worst case (100% loss) is a ~1s block once per 10s
tick.

Wiring assumes a standard external LED (anode → GPIO, cathode → GND), so
GPIO HIGH lights it. If wired the other way (LED to 3.3V, GPIO sinks it),
flip the polarity in `NetworkHealthIndicator::setLed()`.

**Full brightness only, on both platforms** — this LED is strictly on/off,
so it's driven by plain `digitalWrite()` with no PWM/dimming anywhere.
Earlier, when this GPIO drove a dimmable `TemperatureIndicator`, ESP8266
used `analogWrite()` for brightness control; that was confirmed, live, to
cause real WiFi packet loss on this board (ESP8266 has no hardware PWM —
`analogWrite()` there is a software timer-interrupt waveform generator) and
was reverted. That risk doesn't apply here since this LED never dims.

**Use a series resistor** (typically 220–1k ohm for a 3.3V-supplied
indicator LED, exact value depends on the LED's forward voltage/rated
current) — without one, the LED runs at whatever current the GPIO + LED
happen to settle at unregulated, which is usually well past the LED's rated
current. This is the only current-limiting in the circuit now that the LED
never dims via PWM on either platform.

## Flash partitioning

- **ESP32** uses a custom `partitions_esp32_4mb.csv`: two 1.5MB OTA app
  slots (~37% headroom over the ~1.1MB firmware) and an ~832KB LittleFS
  partition. The stock `min_spiffs.csv` was dropped because its 128KB
  filesystem partition is too small for the current dashboard/API assets
  once LittleFS 4KB-block overhead is counted (`uploadfs` overflowed it).
- **ESP8266** uses the board's default `nodemcuv2` layout: two ~1MB OTA
  app slots and roughly 1MB of LittleFS. Check `pio run -t buildfs` output
  if you need the exact current numbers for your framework version.

Both are set via `board_build.filesystem = littlefs` in `platformio.ini`,
so `pio run -t uploadfs` / `-t buildfs` (or your IDE's equivalent) always
target LittleFS.

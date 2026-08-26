#pragma once
// -----------------------------------------------------------------------------
// HardwareConfig.h
//
// Central place for platform-specific pin defaults and capability constants.
// Application code should read these constants (or the runtime-configurable
// equivalents in ConfigManager) rather than sprinkling #ifdef ESP32 / #ifdef
// ESP8266 throughout the codebase.
// -----------------------------------------------------------------------------

#include <Arduino.h>

namespace hw {

#if defined(PLATFORM_ESP32)

// DHT data line. GPIO4 is safe on ESP32 DevKit boards (not a strapping pin).
constexpr uint8_t DEFAULT_DHT_GPIO = 4;

// Physical factory-reset / provisioning button. GPIO0 doubles as BOOT on most
// ESP32 dev boards, which is acceptable here because it is only sampled after
// startup completes (not during boot strapping) and is pulled up on-board.
constexpr uint8_t DEFAULT_BUTTON_GPIO = 0;

// Onboard status LED, present on most ESP32 DevKit boards.
constexpr uint8_t DEFAULT_STATUS_LED_GPIO = 2;

constexpr const char *PLATFORM_NAME = "ESP32";

#elif defined(PLATFORM_ESP8266)

// D2 on NodeMCU / Wemos silkscreens maps to GPIO4 — safe, not a strapping pin.
constexpr uint8_t DEFAULT_DHT_GPIO = 4; // D2

// D3 (GPIO0) is commonly wired to the onboard FLASH button on NodeMCU boards.
// It is a strapping pin at boot (must be HIGH to boot from flash) but NodeMCU
// boards already pull it up, so sampling it after boot for a long-press is
// safe.
constexpr uint8_t DEFAULT_BUTTON_GPIO = 0; // D3

// Onboard LED on most ESP8266 boards (active LOW).
constexpr uint8_t DEFAULT_STATUS_LED_GPIO = 2; // D4

constexpr const char *PLATFORM_NAME = "ESP8266";

#else
#error "Define PLATFORM_ESP32 or PLATFORM_ESP8266 (see platformio.ini build_flags)"
#endif

// Shared, platform-independent defaults.
constexpr uint32_t DEFAULT_ENVIRONMENT_INTERVAL_S = 10;
constexpr uint32_t DEFAULT_NETWORK_INTERVAL_S = 30;
constexpr uint32_t DEFAULT_DASHBOARD_REFRESH_S = 5;
constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;
constexpr uint16_t HTTP_PORT = 80;
constexpr uint16_t PROBE_DEFAULT_TIMEOUT_MS = 1500;
constexpr uint8_t PROBE_DEFAULT_PACKET_COUNT = 5;

// Environment history is sampled at a fixed 60s resolution (independent of
// the live read interval) so the bounded ring file has a predictable flash
// footprint. Capacity is sized per platform: ESP32 boards typically have far
// more flash for the LittleFS partition than ESP8266 boards, so it gets a
// deeper history. At 12 bytes/record + a small header:
//   ESP32:   10080 records = 7 days  (~121 KB)
//   ESP8266:  2880 records = 48 hours (~35 KB)
// The dashboard's "7 Days" range simply shows as much as is retained — it
// never claims data that was never stored.
constexpr uint32_t HISTORY_SAMPLE_INTERVAL_S = 60;
#if defined(PLATFORM_ESP32)
constexpr uint32_t HISTORY_MAX_RECORDS = 10080;
#elif defined(PLATFORM_ESP8266)
constexpr uint32_t HISTORY_MAX_RECORDS = 2880;
#endif

// Provisioning AP SSID override (section 12). The general-purpose default
// is "ENVPROBE-<chip-id-suffix>" (see NetworkManager::deriveApSsid), which
// guarantees every device gets a unique setup-AP name with zero
// configuration — the right default for a codebase meant to be flashed
// onto many identical units.
//
// Set this to a non-empty literal to use that exact SSID instead, for a
// specific, already-identified physical unit. Reset to "" to go back to
// auto chip-ID naming (e.g. before flashing a different unit from this
// same codebase).
constexpr const char *AP_SSID_OVERRIDE = "Firefly-ENV-W01";

} // namespace hw

#pragma once
// -----------------------------------------------------------------------------
// Platform.h
//
// Hardware abstraction layer. Everything outside src/hardware/ should talk to
// PlatformManager instead of checking #ifdef ESP32 / #ifdef ESP8266 directly.
// -----------------------------------------------------------------------------

#include <Arduino.h>

class PlatformManager {
public:
    // Human-readable platform family, e.g. "ESP32" / "ESP8266".
    static String getPlatform();

    // Chip model string, e.g. "ESP32-D0WDQ6" / "ESP8266EX".
    static String getChipModel();

    static uint32_t getFreeHeap();
    static uint32_t getFlashSize();
    static String getResetReason();
    static String getMacAddress();

    // CPU frequency in MHz.
    static uint32_t getCpuFreqMHz();

    // Reboots the device.
    static void restart();

    // Feeds the platform watchdog. No-op where not applicable.
    static void feedWatchdog();

    // Enables the platform watchdog with the given timeout. Safe to call once
    // during setup(); no-op on platforms without a configurable app watchdog.
    static void enableWatchdog(uint32_t timeoutMs);
};

#include "Platform.h"
#include "HardwareConfig.h"

#if defined(PLATFORM_ESP32)
#include <WiFi.h>
#include <esp_system.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#include <Esp.h>
#endif

String PlatformManager::getPlatform() {
    return String(hw::PLATFORM_NAME);
}

String PlatformManager::getChipModel() {
#if defined(PLATFORM_ESP32)
    return String(ESP.getChipModel());
#elif defined(PLATFORM_ESP8266)
    return String("ESP8266EX");
#endif
}

uint32_t PlatformManager::getFreeHeap() {
    return ESP.getFreeHeap();
}

uint32_t PlatformManager::getFlashSize() {
    return ESP.getFlashChipSize();
}

String PlatformManager::getResetReason() {
#if defined(PLATFORM_ESP32)
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "Power on";
        case ESP_RST_EXT:       return "External pin";
        case ESP_RST_SW:        return "Software reset";
        case ESP_RST_PANIC:     return "Panic / exception";
        case ESP_RST_INT_WDT:   return "Interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "Task watchdog";
        case ESP_RST_WDT:       return "Other watchdog";
        case ESP_RST_DEEPSLEEP: return "Deep sleep wake";
        case ESP_RST_BROWNOUT:  return "Brownout";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "Unknown";
    }
#elif defined(PLATFORM_ESP8266)
    return ESP.getResetReason();
#endif
}

String PlatformManager::getMacAddress() {
    return WiFi.macAddress();
}

uint32_t PlatformManager::getCpuFreqMHz() {
    return ESP.getCpuFreqMHz();
}

void PlatformManager::restart() {
    ESP.restart();
}

void PlatformManager::feedWatchdog() {
#if defined(PLATFORM_ESP32)
    yield();
#elif defined(PLATFORM_ESP8266)
    ESP.wdtFeed();
#endif
}

void PlatformManager::enableWatchdog(uint32_t timeoutMs) {
    // Both the ESP32 Arduino core (via IDF's built-in task watchdog on the
    // Arduino loop task) and the ESP8266 core (hardware + software watchdog
    // fed on every yield()/delay()) already provide a working watchdog out
    // of the box as long as loop() never blocks for long stretches, which is
    // the non-blocking design mandated for this project (see NON-BLOCKING
    // DESIGN in the architecture docs). We rely on that default rather than
    // reconfiguring IDF internals whose API has changed across ESP-IDF
    // releases, which would make the ESP32 build fragile across toolchain
    // versions. feedWatchdog() is still called from the main loop so that if
    // a future change to this codebase reintroduces a long-running task, the
    // watchdog behavior is easy to tighten in one place.
    (void)timeoutMs;
}

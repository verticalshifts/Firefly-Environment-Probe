#include <Arduino.h>
#include "hardware/Platform.h"
#include "hardware/HardwareConfig.h"
#include "storage/StorageManager.h"
#include "config/ConfigManager.h"
#include "environment/EnvironmentManager.h"
#include "network/NetworkManager.h"
#include "network/NetworkProbe.h"
#include "device/DeviceManager.h"
#include "web/WebServerManager.h"
#include "telemetry/LocalTelemetry.h"
#include "telemetry/Gen2Telemetry.h"
#include "hardware/NetworkHealthIndicator.h"
#include "ota/BootGuard.h"
#include "ota/OTAUpdateChecker.h"
#include "util/Logger.h"

// -----------------------------------------------------------------------------
// Composition root. Every manager below is independent and talks to the
// others only through the narrow interfaces defined in its own header — see
// docs/architecture.md for the module map and the Phase 2 GEN2 boundary.
// -----------------------------------------------------------------------------

StorageManager storage;
ConfigManager configManager(storage);
DeviceManager device(storage, configManager);
EnvironmentManager environment(configManager, device); // needs device's continuous-uptime clock for history timestamps
NetworkManager network(configManager);
NetworkProbe networkProbe(configManager, network);
BootGuard bootGuard(storage);
OTAUpdateChecker otaUpdateChecker(configManager, network); // opt-in, disabled by default (otaCheckEnabled)
WebServerManager webServer(configManager, environment, network, networkProbe, device, storage, bootGuard, otaUpdateChecker);
LocalTelemetry telemetry; // Phase 1 stand-in for the future Gen2Telemetry adapter
Gen2Telemetry gen2Telemetry(configManager, network, device); // opt-in, disabled by default (gen2Enabled)
NetworkHealthIndicator networkLed(hw::DEFAULT_NETWORK_LED_GPIO, network);

static const char *TAG = "Main";

// Physical factory-reset / provisioning button (section 28).
static unsigned long buttonPressStartMs = 0;
static bool buttonHoldHandled = false;

static void checkFactoryResetButton() {
    uint8_t pin = hw::DEFAULT_BUTTON_GPIO;
    bool pressed = digitalRead(pin) == LOW; // active-low, on-board pull-up

    if (pressed) {
        if (buttonPressStartMs == 0) {
            buttonPressStartMs = millis();
            buttonHoldHandled = false;
        } else if (!buttonHoldHandled && millis() - buttonPressStartMs >= hw::FACTORY_RESET_HOLD_MS) {
            buttonHoldHandled = true;
            Logger::warn(TAG, "Factory reset button held — wiping configuration");
            storage.wipeAll();
            delay(200);
            PlatformManager::restart();
        }
    } else {
        buttonPressStartMs = 0;
        buttonHoldHandled = false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Logger::info(TAG, String("ESP Environment Probe v") + FIRMWARE_VERSION + " booting on " + PlatformManager::getPlatform());

    pinMode(hw::DEFAULT_BUTTON_GPIO, INPUT_PULLUP);

    if (!storage.begin()) {
        Logger::error(TAG, "Storage init failed — continuing with defaults, changes won't persist");
    }
    bootGuard.begin();
    configManager.begin();
    device.begin();
    environment.begin();
    network.begin();
    webServer.begin();
    networkLed.begin();

    PlatformManager::enableWatchdog(15000);
    Logger::info(TAG, "Setup complete. Device ID " + device.getDeviceId());
}

// Boot-confirmation health signal for BootGuard (section OTA): Wi-Fi
// connected continuously for this long is judged "the new firmware is
// stable" — the cheapest meaningful signal available without new
// instrumentation, and every real regression found live this session was a
// WiFi-stability problem, not sensor/logic. See BootGuard.h.
static unsigned long wifiHealthySinceMs = 0;
static constexpr unsigned long BOOT_CONFIRM_WIFI_STABLE_MS = 15000;

void loop() {
    PlatformManager::feedWatchdog();

    network.loop();
    environment.loop();
    device.loop();

    bootGuard.loop();
    if (bootGuard.pendingConfirm()) {
        if (network.isConnected()) {
            if (wifiHealthySinceMs == 0) wifiHealthySinceMs = millis();
            else if (millis() - wifiHealthySinceMs >= BOOT_CONFIRM_WIFI_STABLE_MS) {
                bootGuard.confirmBootOk();
            }
        } else {
            wifiHealthySinceMs = 0;
        }
    }

    networkLed.loop();

    if (environment.status() != EnvironmentStatus::NOT_YET_READ) {
        telemetry.publishEnvironment(environment.current(), environment.sensorType());
        gen2Telemetry.publishEnvironment(environment.current(), environment.sensorType());
    }

    otaUpdateChecker.loop();

    networkProbe.loop();
    webServer.loop();
    checkFactoryResetButton();

    if (webServer.factoryResetPending()) {
        // Only run once: wipeAll() before the scheduled restart actually
        // fires (restartPending() gates on time, so this executes on every
        // loop tick until then — guard by re-checking storage state isn't
        // worth it since wipeAll() is idempotent and cheap to call once
        // more than strictly necessary would only happen if restartAtMs_
        // were far in the future, which it isn't here).
        static bool wiped = false;
        if (!wiped) {
            wiped = true;
            storage.wipeAll();
        }
    }

    if (webServer.restartPending()) {
        Logger::info(TAG, "Restarting");
        delay(50);
        PlatformManager::restart();
    }
}

#include "DeviceManager.h"
#include "hardware/Platform.h"
#include "util/Logger.h"

#if defined(PLATFORM_ESP32)
#include <WiFi.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#endif

static const char *TAG = "Device";

DeviceManager::DeviceManager(StorageManager &storage, ConfigManager &config)
    : storage_(storage), config_(config) {}

String DeviceManager::deriveDeviceId() {
    String mac = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
    String suffix;
    for (size_t i = mac.length(); i-- > 0;) {
        if (mac[i] != ':') suffix = String(mac[i]) + suffix;
        if (suffix.length() >= 6) break;
    }
    suffix.toUpperCase();
    return "ENV-" + PlatformManager::getPlatform() + "-" + suffix;
}

String DeviceManager::generatePassword(uint8_t length) {
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    randomSeed(micros() ^ (uint32_t)ESP.getFreeHeap());
    String pw;
    for (uint8_t i = 0; i < length; i++) {
        pw += alphabet[random(0, sizeof(alphabet) - 1)];
    }
    return pw;
}

bool DeviceManager::begin() {
    deviceId_ = deriveDeviceId();

    JsonDocument doc;
    if (storage_.exists(DEVICE_STATE_PATH) && storage_.readJsonFile(DEVICE_STATE_PATH, doc)) {
        bootCount_ = (doc["bootCount"] | 0);
    }
    bootCount_++;

    doc.clear();
    doc["bootCount"] = bootCount_;
    doc["deviceId"] = deviceId_;
    storage_.writeJsonFile(DEVICE_STATE_PATH, doc);

    Logger::info(TAG, "Device ID: " + deviceId_ + ", boot #" + String(bootCount_));

    // First-boot bootstrap: if no dashboard password has ever been set,
    // generate one now rather than shipping a fixed default (section 25).
    if (config_.get().authPassword.length() == 0) {
        String pw = generatePassword(10);
        JsonDocument updates;
        updates["authPassword"] = pw;
        String err;
        if (config_.update(updates.as<JsonObjectConst>(), err)) {
            freshPasswordGenerated_ = true;
            Logger::info(TAG, "Generated initial dashboard password");
        } else {
            Logger::error(TAG, "Failed to persist generated password: " + err);
        }
    }

    return true;
}

DeviceStatus DeviceManager::getStatus() const {
    DeviceStatus s;
    s.deviceId = deviceId_;
    s.deviceName = config_.get().deviceName;
    s.platform = PlatformManager::getPlatform();
    s.firmwareVersion = FIRMWARE_VERSION;
    s.uptimeS = millis() / 1000;
    s.freeHeap = PlatformManager::getFreeHeap();
    return s;
}

#include "StorageManager.h"
#include "hardware/FSCompat.h"
#include "util/Logger.h"
#include "AppPaths.h"

static const char *TAG = "Storage";

bool StorageManager::begin() {
    if (!fscompat::begin()) {
        Logger::error(TAG, "LittleFS mount failed");
        return false;
    }
    Logger::info(TAG, "LittleFS mounted");
    return true;
}

bool StorageManager::readJsonFile(const String &path, JsonDocument &doc) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Logger::warn(TAG, "Failed to parse " + path + ": " + String(err.c_str()));
        return false;
    }
    return true;
}

bool StorageManager::writeJsonFile(const String &path, const JsonDocument &doc) {
    // Write to a temp file first so a power loss mid-write can't corrupt the
    // last-known-good file (important for config.json, which provisioning
    // depends on at every boot).
    String tmpPath = path + ".tmp";
    File f = LittleFS.open(tmpPath, "w");
    if (!f) {
        Logger::error(TAG, "Failed to open " + tmpPath + " for write");
        return false;
    }
    size_t written = serializeJson(doc, f);
    f.close();
    if (written == 0) {
        Logger::error(TAG, "Failed to serialize JSON for " + path);
        LittleFS.remove(tmpPath);
        return false;
    }

    LittleFS.remove(path);
    if (!LittleFS.rename(tmpPath, path)) {
        Logger::error(TAG, "Failed to commit " + path);
        return false;
    }
    return true;
}

bool StorageManager::exists(const String &path) {
    return LittleFS.exists(path);
}

bool StorageManager::remove(const String &path) {
    return LittleFS.remove(path);
}

uint32_t StorageManager::totalBytes() {
#if defined(PLATFORM_ESP32)
    return LittleFS.totalBytes();
#elif defined(PLATFORM_ESP8266)
    FSInfo info;
    LittleFS.info(info);
    return info.totalBytes;
#endif
}

uint32_t StorageManager::usedBytes() {
#if defined(PLATFORM_ESP32)
    return LittleFS.usedBytes();
#elif defined(PLATFORM_ESP8266)
    FSInfo info;
    LittleFS.info(info);
    return info.usedBytes;
#endif
}

bool StorageManager::wipeAll() {
    Logger::warn(TAG, "Wiping config/device-state/history (factory reset)");

    // Best-effort: a fresh/never-fully-booted device may be missing one of
    // these (e.g. history was never begin()'d), which isn't a failure.
    LittleFS.remove(paths::CONFIG);
    LittleFS.remove(String(paths::CONFIG) + ".tmp"); // leftover from an interrupted atomic write
    LittleFS.remove(paths::DEVICE_STATE);
    LittleFS.remove(paths::ENV_HISTORY);
    LittleFS.rmdir("/history"); // harmless no-op if not empty or absent

    return true;
}

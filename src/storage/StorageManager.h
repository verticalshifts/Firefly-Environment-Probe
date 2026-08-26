#pragma once
// -----------------------------------------------------------------------------
// StorageManager.h
//
// Thin abstraction over LittleFS used by every other manager. Nothing outside
// this file (and CircularLog, which is the historical-data specialization)
// should call LittleFS directly — that keeps the persistence backend an
// implementation detail, per section 24.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <ArduinoJson.h>

class StorageManager {
public:
    bool begin();

    bool readJsonFile(const String &path, JsonDocument &doc);
    bool writeJsonFile(const String &path, const JsonDocument &doc);

    bool exists(const String &path);
    bool remove(const String &path);

    uint32_t totalBytes();
    uint32_t usedBytes();

    // Removes config, device state, and history (see AppPaths.h) — the
    // runtime-written data a factory reset is meant to clear. Deliberately
    // does NOT format the filesystem: the static dashboard files under /,
    // /css/, /js/ (uploaded via `pio run -t uploadfs`) are not regenerated
    // by the firmware, so wiping them would leave the device serving
    // "file missing" instead of the provisioning page. Used by factory
    // reset (section 27).
    bool wipeAll();
};

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

    // Removes every file this firmware owns and reformats the filesystem.
    // Used by factory reset (section 27).
    bool wipeAll();
};

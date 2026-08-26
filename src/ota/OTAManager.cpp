#include "OTAManager.h"
#if defined(PLATFORM_ESP32)
#include <Update.h>
#elif defined(PLATFORM_ESP8266)
#include <Updater.h>
#endif
#include "util/Logger.h"

static const char *TAG = "OTA";

// ESP32's UpdateClass exposes errorString(); ESP8266's exposes
// getErrorString() (returning String by value instead of const char*).
static String updateErrorString() {
#if defined(PLATFORM_ESP32)
    return String(Update.errorString());
#elif defined(PLATFORM_ESP8266)
    return Update.getErrorString();
#endif
}

bool OTAManager::start(size_t sizeHint) {
    lastError_ = "";
#if defined(PLATFORM_ESP32)
    bool ok = Update.begin(sizeHint > 0 ? sizeHint : UPDATE_SIZE_UNKNOWN);
#elif defined(PLATFORM_ESP8266)
    bool ok = Update.begin(sizeHint > 0 ? sizeHint : (uint32_t)0xFFFFFFFF);
#endif
    if (!ok) {
        lastError_ = updateErrorString();
        Logger::error(TAG, "Update.begin failed: " + lastError_);
    } else {
        Logger::info(TAG, "OTA update started");
    }
    return ok;
}

bool OTAManager::write(uint8_t *data, size_t len) {
    size_t written = Update.write(data, len);
    if (written != len) {
        lastError_ = updateErrorString();
        Logger::error(TAG, "Update.write short write: " + lastError_);
        return false;
    }
    return true;
}

bool OTAManager::finish() {
    bool ok = Update.end(true);
    if (!ok) {
        lastError_ = updateErrorString();
        Logger::error(TAG, "Update.end failed: " + lastError_);
    } else {
        Logger::info(TAG, "OTA update complete, rebooting into new firmware");
    }
    return ok;
}

#include "BootGuard.h"
#include "hardware/Platform.h"
#include "storage/AppPaths.h"
#include "util/Logger.h"

#if defined(PLATFORM_ESP32)
#include <esp_ota_ops.h>
#include <esp_partition.h>
#endif

static const char *TAG = "BootGuard";

BootGuard::BootGuard(StorageManager &storage) : storage_(storage) {}

void BootGuard::loadState() {
    JsonDocument doc;
    if (!storage_.readJsonFile(paths::OTA_BOOT_STATE, doc)) return; // absent = nothing pending, all defaults hold
    pendingConfirm_ = doc["pendingConfirm"] | false;
    attemptCount_ = doc["attemptCount"] | 0;
    fallbackPartition_ = doc["fallbackPartition"] | "";
    attemptedVersion_ = doc["attemptedVersion"] | "";
    lastFailedToConfirm_ = doc["lastFailedToConfirm"] | false;
}

void BootGuard::saveState() {
    JsonDocument doc;
    doc["pendingConfirm"] = pendingConfirm_;
    doc["attemptCount"] = attemptCount_;
    doc["fallbackPartition"] = fallbackPartition_;
    doc["attemptedVersion"] = attemptedVersion_;
    doc["lastFailedToConfirm"] = lastFailedToConfirm_;
    storage_.writeJsonFile(paths::OTA_BOOT_STATE, doc);
}

void BootGuard::begin() {
    loadState();
    if (!pendingConfirm_) return;

    // A confirm was pending when we last rebooted into this state, and here
    // we are booting again — either the previous boot legitimately never
    // got a chance to confirm yet (still within budget), or it crash-looped
    // before reaching main.cpp's confirm logic. Either way, count it.
    attemptCount_++;
    saveState();
    Logger::warn(TAG, "OTA boot-confirm pending (attempt " + String(attemptCount_) + "/" +
                           String(MAX_CONFIRM_ATTEMPTS) + ")");

    if (attemptCount_ > MAX_CONFIRM_ATTEMPTS) {
        Logger::error(TAG, "OTA update failed to confirm across " + String(MAX_CONFIRM_ATTEMPTS) +
                                " boot attempts — treating as a failed update");
        onConfirmFailure();
        return;
    }

    armedAtMs_ = millis();
}

void BootGuard::loop() {
    if (!pendingConfirm_ || confirmed_) return;
    if (millis() - armedAtMs_ >= CONFIRM_TIMEOUT_MS) {
        Logger::error(TAG, "OTA boot did not confirm within " + String(CONFIRM_TIMEOUT_MS / 1000) + "s");
        onConfirmFailure();
    }
}

void BootGuard::confirmBootOk() {
    if (!pendingConfirm_ || confirmed_) return;
    confirmed_ = true;
    pendingConfirm_ = false;
    attemptCount_ = 0;
    lastFailedToConfirm_ = false;
    saveState();
    Logger::info(TAG, "OTA boot confirmed healthy (v" + attemptedVersion_ + ")");
}

void BootGuard::armPendingConfirm(const String &attemptedVersion) {
    pendingConfirm_ = true;
    confirmed_ = false;
    attemptCount_ = 0;
    attemptedVersion_ = attemptedVersion;
    lastFailedToConfirm_ = false;
#if defined(PLATFORM_ESP32)
    const esp_partition_t *running = esp_ota_get_running_partition();
    fallbackPartition_ = running ? String(running->label) : "";
#else
    fallbackPartition_ = "";
#endif
    armedAtMs_ = millis();
    saveState();
    Logger::info(TAG, "OTA boot-confirm armed for the next boot (fallback=" + fallbackPartition_ + ")");
}

void BootGuard::onConfirmFailure() {
#if defined(PLATFORM_ESP32)
    revertEsp32();
#else
    // ESP8266: no automatic revert — see BootGuard.h's header comment for
    // why. Report and keep running whatever booted.
    pendingConfirm_ = false;
    lastFailedToConfirm_ = true;
    saveState();
    Logger::error(TAG, "OTA update did not self-confirm — no automatic revert available on this "
                        "platform (see docs/architecture.md). Device continues running the new "
                        "image; check /api/ota/status.");
#endif
}

#if defined(PLATFORM_ESP32)
void BootGuard::revertEsp32() {
    pendingConfirm_ = false;
    lastFailedToConfirm_ = true;

    if (fallbackPartition_.length() == 0) {
        Logger::error(TAG, "No fallback partition recorded — cannot revert automatically");
        saveState();
        return;
    }

    const esp_partition_t *fallback = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, fallbackPartition_.c_str());
    if (!fallback) {
        Logger::error(TAG, "Fallback partition '" + fallbackPartition_ + "' not found — cannot revert");
        saveState();
        return;
    }

    // Clear the flag BEFORE restarting so the reverted (old, known-good)
    // image's next boot doesn't see pendingConfirm=true and try to
    // "revert" again.
    saveState();
    Logger::error(TAG, "Reverting boot partition to '" + fallbackPartition_ + "' and restarting");
    esp_ota_set_boot_partition(fallback);
    delay(50);
    PlatformManager::restart();
}
#endif

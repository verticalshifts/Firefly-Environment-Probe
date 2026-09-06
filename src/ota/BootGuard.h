#pragma once
// -----------------------------------------------------------------------------
// BootGuard.h
//
// Boot-confirmation safety for OTA updates. Separate from OTAManager (which
// stays a thin, stateless Update.h wrapper) because this owns persisted
// state and a millis() timer, and diverges meaningfully by platform.
//
// The idea: right after an OTA finish() succeeds, arm a "pending confirm"
// flag (persisted to LittleFS, so it survives a hard power cycle — the
// scenario most likely after a bad flash leaves the device unresponsive).
// The new firmware must prove itself healthy (main.cpp judges this: Wi-Fi
// connected continuously for 15s) within CONFIRM_TIMEOUT_MS or across
// MAX_CONFIRM_ATTEMPTS boot-crash cycles, or this class treats it as a
// failed update.
//
// ESP32 gets a real automatic revert: armPendingConfirm() captures the
// currently-running (known-good) partition's label, and a failed
// confirmation calls esp_ota_set_boot_partition() back to it via the
// esp_ota_ops/esp_partition APIs Arduino-ESP32 exposes — this works without
// needing the bootloader's native CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
// feature, which isn't compiled into the framework's prebuilt bootloader.
//
// ESP8266 gets report-only: no automatic revert is attempted. This is
// deliberate, not an oversight — there is no equivalently stable, documented
// Arduino-core API for it (only low-level, undocumented-for-this-purpose
// eboot_command manipulation), and this codebase has zero prior OTA-rollback
// track record to justify that risk. A failed confirmation on ESP8266 just
// persists lastFailedToConfirm=true (visible via /api/ota/status, survives
// a power cycle) and logs clearly — the device keeps running whatever it
// booted into. See docs/architecture.md's "OTA rollback safety" section.
//
// Known, disclosed gap on both platforms: if a bad image crashes before
// loop() ever runs once, only the flash-persisted attemptCount (not the
// in-RAM CONFIRM_TIMEOUT_MS timer) catches it, after MAX_CONFIRM_ATTEMPTS
// full boot-crash cycles.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "storage/StorageManager.h"

class BootGuard {
public:
    explicit BootGuard(StorageManager &storage);

    // Call once from setup(), right after storage.begin(). Loads
    // /ota_boot.json; if a confirm is pending, arms the in-RAM timeout
    // window and bumps+persists attemptCount immediately (so a crash before
    // loop() is ever reached is still visible across boots).
    void begin();

    // Non-blocking millis() tick from main.cpp's loop(). No-op unless a
    // confirm is pending. Triggers the failure path if CONFIRM_TIMEOUT_MS
    // elapses without confirmBootOk() having been called.
    void loop();

    // Call once main.cpp judges this boot healthy. Idempotent.
    void confirmBootOk();

    // Call right after OTAManager::finish() succeeds (either OTA path).
    // attemptedVersion is diagnostic-only (may be "" for the manual-upload
    // path, which doesn't know the new binary's version ahead of time).
    void armPendingConfirm(const String &attemptedVersion);

    bool pendingConfirm() const { return pendingConfirm_; }
    bool lastBootFailedToConfirm() const { return lastFailedToConfirm_; }

private:
    StorageManager &storage_;

    bool pendingConfirm_ = false;
    bool confirmed_ = false;
    bool lastFailedToConfirm_ = false;
    uint8_t attemptCount_ = 0;
    String fallbackPartition_;
    String attemptedVersion_;
    unsigned long armedAtMs_ = 0;

    static constexpr uint32_t CONFIRM_TIMEOUT_MS = 120000; // 2 minutes
    static constexpr uint8_t MAX_CONFIRM_ATTEMPTS = 3;

    void loadState();
    void saveState();
    void onConfirmFailure();

#if defined(PLATFORM_ESP32)
    void revertEsp32();
#endif
};

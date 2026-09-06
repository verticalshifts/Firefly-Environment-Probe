#pragma once
// -----------------------------------------------------------------------------
// DeviceManager.h
//
// Device identity, boot bookkeeping (section 23), and first-boot bootstrap
// (auto-generating a local dashboard password, section 25).
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "config/ConfigManager.h"
#include "storage/StorageManager.h"
#include "storage/AppPaths.h"

// Normalized internal data model (section 32).
struct DeviceStatus {
    String deviceId;
    String deviceName;
    String platform;
    String firmwareVersion;
    uint32_t uptimeS;
    uint32_t freeHeap;
};

class DeviceManager {
public:
    DeviceManager(StorageManager &storage, ConfigManager &config);

    // Loads/creates /device.json (boot count), and — on true first boot —
    // generates a random local-dashboard password if none is configured.
    bool begin();

    // Non-blocking millis()-gated tick from main.cpp's loop() — periodically
    // persists the continuous-uptime offset (see getContinuousUptimeS())
    // so it survives an ungraceful reset with only a few minutes of drift.
    void loop();

    String getDeviceId() const { return deviceId_; }
    uint32_t getBootCount() const { return bootCount_; }

    // Session-relative uptime (resets to ~0 on every boot) — used e.g. by
    // the OTA reboot-confirmation flow in Settings, which specifically
    // needs "did uptime just reset" as its success signal. Do NOT use this
    // for anything persisted/compared across reboots (see below).
    uint32_t getUptimeSeconds() const { return millis() / 1000; }

    // Monotonically increasing across reboots (a persisted offset + this
    // session's own millis()/1000), unlike getUptimeSeconds(). This is what
    // EnvironmentManager stamps history points with — using plain
    // millis()/1000 there meant every reboot reset the clock those points
    // are measured against while the *history file itself* (LittleFS)
    // survived the reboot, so old and new points ended up interleaved with
    // wildly non-monotonic timestamps — confirmed live as the cause of the
    // dashboard's history charts rendering as scrambled/crossed lines after
    // this device's many reboots during development. Accurate to within
    // one loop()-persist interval of an ungraceful reset (a few minutes at
    // most), which is a fine trade for a monitoring history feature.
    uint32_t getContinuousUptimeS() const { return uptimeOffsetS_ + (uint32_t)(millis() / 1000); }

    // True only for the single boot on which a fresh dashboard password was
    // generated — used to surface it once on the provisioning success page.
    bool freshPasswordGenerated() const { return freshPasswordGenerated_; }

    DeviceStatus getStatus() const;

private:
    StorageManager &storage_;
    ConfigManager &config_;

    String deviceId_;
    uint32_t bootCount_ = 0;
    bool freshPasswordGenerated_ = false;

    uint32_t uptimeOffsetS_ = 0;         // cumulative seconds from all boots before this one
    unsigned long lastOffsetSaveMs_ = 0; // millis() at last persist, for the loop() gate
    static constexpr uint32_t OFFSET_SAVE_INTERVAL_MS = 300000; // 5 min

    static constexpr const char *DEVICE_STATE_PATH = paths::DEVICE_STATE;

    String deriveDeviceId();
    String generatePassword(uint8_t length);
    void saveState(); // writes deviceId/bootCount/uptimeOffsetS together
};

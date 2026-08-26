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

    String getDeviceId() const { return deviceId_; }
    uint32_t getBootCount() const { return bootCount_; }
    uint32_t getUptimeSeconds() const { return millis() / 1000; }

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

    static constexpr const char *DEVICE_STATE_PATH = "/device.json";

    String deriveDeviceId();
    String generatePassword(uint8_t length);
};

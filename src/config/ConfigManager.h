#pragma once
// -----------------------------------------------------------------------------
// ConfigManager.h
//
// Owns the single versioned configuration document (section 24) covering
// Wi-Fi, sensor selection, probe targets, intervals, alert thresholds, and
// local dashboard auth. Persisted as JSON via StorageManager, so the app
// never touches LittleFS/Preferences directly for configuration.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "storage/StorageManager.h"

struct DeviceConfig {
    uint16_t configVersion = CONFIG_SCHEMA_VERSION;

    // Identity
    String deviceName = "Environment Probe";

    // Wi-Fi
    String wifiSsid = "";
    String wifiPassword = "";
    bool useStaticIp = false;
    String staticIp = "";
    String staticGateway = "";
    String staticSubnet = "255.255.255.0";
    String staticDns = "";
    uint8_t wifiConnectAttempts = 20; // ~10s at 500ms/attempt before giving up

    // Local dashboard auth (section 25)
    String authUsername = "admin";
    String authPassword = ""; // generated on first boot if empty, see DeviceManager

    // mDNS
    String mdnsHostname = ""; // derived from deviceName if empty

    // Sensor (section 7)
    String sensorType = "DHT22"; // "DHT11" | "DHT22"
    uint8_t sensorGpio = 0;      // 0 = use platform default

    // Sampling intervals (section 21), all in seconds
    uint32_t environmentIntervalS = 10;
    uint32_t networkIntervalS = 30;
    uint32_t dashboardRefreshS = 5;

    // Network ground probe targets (section 18-19)
    String gatewayTarget = ""; // empty = use DHCP-learned gateway
    String pingTarget1 = "8.8.8.8";
    String pingTarget2 = "1.1.1.1";
    String dnsDomain = "google.com";
    String httpTarget = "https://example.com";
    uint32_t probeTimeoutMs = 1500;
    uint8_t probePacketCount = 5;

    // Alert thresholds (section 35)
    float tempHighC = 35.0f;
    float tempLowC = 10.0f;
    float humidityHighPct = 80.0f;
    float humidityLowPct = 30.0f;
    int rssiLowDbm = -80;
    float latencyHighMs = 100.0f;
    float packetLossHighPct = 10.0f;
};

class ConfigManager {
public:
    explicit ConfigManager(StorageManager &storage);

    // Loads config.json, or writes+loads defaults if absent/invalid.
    bool begin();

    const DeviceConfig &get() const { return config_; }

    // Applies `updates` (a partial JSON document — only present fields are
    // changed) and persists. Returns false if validation fails; the
    // in-memory config is left unchanged on failure.
    bool update(JsonObjectConst updates, String &errorOut);

    bool save();

    // True once wifiSsid is non-empty, i.e. the device has left first-boot
    // provisioning at least once.
    bool isProvisioned() const { return config_.wifiSsid.length() > 0; }

    // Serializes the current config to `doc`. If `redactSecrets` is true,
    // wifiPassword/authPassword are omitted entirely (section 25: never
    // expose credentials through GET APIs).
    void toJson(JsonDocument &doc, bool redactSecrets) const;

    static constexpr const char *CONFIG_PATH = "/config.json";

private:
    StorageManager &storage_;
    DeviceConfig config_;

    void fromJson(JsonDocument &doc);
    bool validate(const DeviceConfig &c, String &errorOut) const;
};

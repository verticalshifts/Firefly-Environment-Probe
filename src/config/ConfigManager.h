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
#include "storage/AppPaths.h"
#include "hardware/HardwareConfig.h"
#include "network/MonitorDef.h"

struct DeviceConfig {
    uint16_t configVersion = CONFIG_SCHEMA_VERSION;

    // Identity
    String deviceName = "Environment Probe";

    // Wi-Fi
    String wifiSsid = "";
    String wifiPassword = ""; // WPA2-Personal PSK; ignored when wifiAuthMode is "enterprise"
    // WPA2-Enterprise (PEAP/MSCHAPv2), an alternative to the PSK above.
    // wifiUsername serves as BOTH the EAP outer identity and the inner
    // username (one field, by design — correct for the vast majority of
    // corporate Wi-Fi; not the anonymous-outer-identity setups eduroam-style
    // networks sometimes use). Only PEAP is supported, not TLS/TTLS/WPA3-
    // Enterprise, and neither platform validates the RADIUS server's CA
    // certificate — see docs/architecture.md's Auth model.
    String wifiAuthMode = "personal"; // "personal" | "enterprise"
    String wifiUsername = "";         // EAP identity + username; NOT a secret, round-trips via GET
    String wifiEapPassword = "";      // EAP password; secret — redacted like authPassword/gen2LicenseKey
    bool useStaticIp = false;
    String staticIp = "";
    String staticGateway = "";
    String staticSubnet = "255.255.255.0";
    String staticDns = "";
    // Number of full connect attempts (fresh scan + WiFi.begin() each,
    // not polls of one attempt) during the initial blocking connect in
    // setup(), spread evenly across a fixed ~60s total budget — see
    // NetworkManager::WIFI_CONNECT_TOTAL_BUDGET_MS. Falls back to the
    // provisioning AP only after all attempts in that window are exhausted.
    uint8_t wifiConnectAttempts = 3;

    // Local dashboard auth (section 25)
    String authUsername = "admin";
    String authPassword = ""; // generated on first boot if empty, see DeviceManager

    // mDNS
    String mdnsHostname = ""; // derived from deviceName if empty

    // Sensor (section 7). The deployed units use DHT11; override to "DHT22"
    // in Settings for a board actually fitted with one.
    String sensorType = "DHT11"; // "DHT11" | "DHT22"
    uint8_t sensorGpio = 0;      // 0 = use platform default

    // Sampling intervals (section 21), all in seconds
    uint32_t environmentIntervalS = 10;
    uint32_t networkIntervalS = 30;
    uint32_t dashboardRefreshS = 5;

    // Network monitors (section 18-19). A variable-length, user-managed list
    // that GEN2 can also dispatch into — this replaces the old fixed
    // gatewayTarget/pingTarget1/pingTarget2/dnsDomain/httpTarget scalars,
    // which are migrated into this list exactly once at schema version 3
    // (see ConfigManager::fromJson) and then dropped from config.json.
    //
    // A fixed array rather than std::vector: heap predictability matters more
    // than elasticity here, and the count is capped anyway.
    MonitorDef monitors[hw::MAX_MONITORS];
    uint8_t monitorCount = 0;
    uint32_t probeTimeoutMs = 1500;
    // Each ping-based probe (Gateway, Probe Target 1/2) blocks loop() for
    // roughly this many seconds (standard ~1s/packet pacing) — confirmed
    // live that 5 packets meant ~5s per probe, ~15s combined across all
    // three back-to-back (they share one interval, so all become "due"
    // together), enough to starve WiFi servicing and cause real ping
    // packet loss to the device itself. 1 keeps each probe to ~1s.
    uint8_t probePacketCount = 1;

    // Alert thresholds (section 35)
    float tempHighC = 35.0f;
    float tempLowC = 10.0f;
    float humidityHighPct = 80.0f;
    float humidityLowPct = 30.0f;
    int rssiLowDbm = -80;
    float latencyHighMs = 100.0f;
    float packetLossHighPct = 10.0f;

    // GEN2 Bullseye integration (Phase 2 seam — opt-in, disabled by default;
    // see docs/architecture.md's "Phase 2 boundary" and Gen2Telemetry.h)
    bool gen2Enabled = false;
    String gen2ServerUrl = "https://gen2bullseye.com"; // host only — firmware appends /api/groundprobe
    String gen2OrgId = "";
    String gen2LicenseKey = "";   // secret — redacted in toJson(redactSecrets=true)
    String gen2MonitorName = ""; // blank = falls back to deviceName
    uint32_t gen2IntervalS = 60; // min HTTPS POST spacing to GEN2, independent of environmentIntervalS
    // Pull monitors GEN2 has dispatched for this license key and apply them
    // locally (src/telemetry/Gen2MonitorSync.h). Independent of gen2Enabled:
    // a device can sync its monitor list without publishing readings, or the
    // reverse. Needs gen2LicenseKey + gen2OrgId either way.
    bool gen2SyncEnabled = false;
    uint32_t gen2SyncIntervalS = 120;
    // Publish each monitor's own status to GEN2 as a named beacon, which is
    // what makes a locally-added monitor appear on GEN2's dashboard — and
    // what keeps any monitor from being aged out to DOWN by GEN2's staleness
    // checker. Off by default because every beacon is a blocking TLS POST,
    // one per monitor per gen2IntervalS; but gen2SyncEnabled forces it on
    // regardless (see Gen2Telemetry::publishNetwork), because a synced
    // monitor that is never reported will always alarm.
    bool gen2PublishMonitors = false;

    // Firefly-CAP100 IoT Gateway (opt-in, disabled by default). A secondary
    // publish path that runs independently of — never instead of — the GEN2
    // one above: the gateway buffers/aggregates readings on the LAN and
    // forwards them to GEN2 itself. See src/telemetry/IotGatewayTelemetry.h.
    bool iotgwEnabled = false;
    String iotgwUrl = "";         // full ingest URL, e.g. http://192.168.0.24/ajax/iotgw/ingest.php
    String iotgwToken = "";       // secret bearer token (iot_...) — redacted in toJson(redactSecrets=true)
    uint32_t iotgwIntervalS = 60; // min POST spacing to the gateway, independent of environmentIntervalS

    // OTA auto-update checking (opt-in, disabled by default; notify-only —
    // never auto-installs, see src/ota/OTAUpdateChecker.h). The GitHub repo
    // checked against is a compile-time constant (OTA_GITHUB_OWNER/REPO in
    // platformio.ini), not a Settings field — see that file's comment.
    bool otaCheckEnabled = false;
    uint32_t otaCheckIntervalS = 21600; // 6h
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

    // Bumped on every change to the monitor list, so NetworkProbe can notice
    // that its per-monitor scheduling state is stale without having to diff
    // the list itself. Wraps harmlessly.
    uint32_t monitorsGeneration() const { return monitorsGeneration_; }

    // Monitor mutations used by GEN2 sync (src/telemetry/Gen2MonitorSync.cpp).
    // Both persist immediately. addOrUpdateMonitor() matches an existing
    // monitor by name and updates it in place rather than creating a
    // duplicate, so a re-dispatched job is idempotent.
    bool addOrUpdateMonitor(const MonitorDef &m, String &errorOut);
    bool removeMonitorByName(const String &name);

    // True once wifiSsid is non-empty, i.e. the device has left first-boot
    // provisioning at least once.
    bool isProvisioned() const { return config_.wifiSsid.length() > 0; }

    // Serializes the current config to `doc`. If `redactSecrets` is true,
    // wifiPassword/authPassword are omitted entirely (section 25: never
    // expose credentials through GET APIs).
    void toJson(JsonDocument &doc, bool redactSecrets) const;

    static constexpr const char *CONFIG_PATH = paths::CONFIG;

private:
    StorageManager &storage_;
    DeviceConfig config_;
    uint32_t monitorsGeneration_ = 0;

    void fromJson(JsonDocument &doc);
    bool validate(const DeviceConfig &c, String &errorOut) const;

    // Seeds the monitor list from the pre-v3 fixed probe-target keys. Reads
    // them straight off the raw document because they no longer exist as
    // DeviceConfig fields.
    static void seedMonitorsFromLegacy(JsonDocument &doc, DeviceConfig &c);
};

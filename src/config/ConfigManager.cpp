#include "ConfigManager.h"
#include "hardware/HardwareConfig.h"
#include "util/Logger.h"

static const char *TAG = "Config";

// Short opaque id for a monitor. Only has to be unique within one device's
// list and stable once persisted — it is what live probe results are matched
// against, so it must survive a reboot and must not be a list index.
static String makeMonitorId() {
    static uint32_t seq = 0;
    uint32_t v = (uint32_t)micros() ^ (++seq * 2654435761u);
    char buf[9];
    snprintf(buf, sizeof(buf), "%08x", (unsigned)v);
    return String(buf);
}

ConfigManager::ConfigManager(StorageManager &storage) : storage_(storage) {}

bool ConfigManager::begin() {
    JsonDocument doc;
    if (storage_.exists(CONFIG_PATH) && storage_.readJsonFile(CONFIG_PATH, doc)) {
        fromJson(doc);
        if (config_.configVersion != CONFIG_SCHEMA_VERSION) {
            // Phase 1 has a single schema version; a future migration step
            // would go here. For now, keep whatever fields parsed and bump
            // the version forward so it round-trips cleanly next save.
            Logger::warn(TAG, "Config schema version mismatch, migrating in place");
            config_.configVersion = CONFIG_SCHEMA_VERSION;
        }
        Logger::info(TAG, "Loaded config.json");
    } else {
        Logger::info(TAG, "No valid config.json found, using defaults");
        config_ = DeviceConfig();
        // A factory-fresh device still needs a starting monitor set —
        // DeviceConfig has an empty list, and without this the device would
        // monitor nothing at all, where the pre-v3 firmware always had five.
        // Seeding from an empty document yields exactly those five defaults.
        JsonDocument empty;
        seedMonitorsFromLegacy(empty, config_);
    }

    if (config_.sensorGpio == 0) {
        config_.sensorGpio = hw::DEFAULT_DHT_GPIO;
    }

    return save();
}

bool ConfigManager::save() {
    JsonDocument doc;
    toJson(doc, /*redactSecrets=*/false);
    return storage_.writeJsonFile(CONFIG_PATH, doc);
}

void ConfigManager::fromJson(JsonDocument &doc) {
    DeviceConfig c;
    c.configVersion = doc["configVersion"] | c.configVersion;
    c.deviceName = doc["deviceName"] | c.deviceName;

    c.wifiSsid = doc["wifiSsid"] | c.wifiSsid;
    c.wifiPassword = doc["wifiPassword"] | c.wifiPassword;
    c.wifiAuthMode = doc["wifiAuthMode"] | c.wifiAuthMode;
    c.wifiUsername = doc["wifiUsername"] | c.wifiUsername;
    c.wifiEapPassword = doc["wifiEapPassword"] | c.wifiEapPassword;
    c.useStaticIp = doc["useStaticIp"] | c.useStaticIp;
    c.staticIp = doc["staticIp"] | c.staticIp;
    c.staticGateway = doc["staticGateway"] | c.staticGateway;
    c.staticSubnet = doc["staticSubnet"] | c.staticSubnet;
    c.staticDns = doc["staticDns"] | c.staticDns;
    c.wifiConnectAttempts = doc["wifiConnectAttempts"] | c.wifiConnectAttempts;

    c.authUsername = doc["authUsername"] | c.authUsername;
    c.authPassword = doc["authPassword"] | c.authPassword;

    c.mdnsHostname = doc["mdnsHostname"] | c.mdnsHostname;

    c.sensorType = doc["sensorType"] | c.sensorType;
    c.sensorGpio = doc["sensorGpio"] | c.sensorGpio;

    c.environmentIntervalS = doc["environmentInterval"] | c.environmentIntervalS;
    c.networkIntervalS = doc["networkInterval"] | c.networkIntervalS;
    c.dashboardRefreshS = doc["dashboardRefresh"] | c.dashboardRefreshS;

    c.probeTimeoutMs = doc["probeTimeoutMs"] | c.probeTimeoutMs;
    c.probePacketCount = doc["probePacketCount"] | c.probePacketCount;

    // Monitors exist from schema v3 onward. A malformed entry is skipped
    // rather than failing the whole load — one corrupt monitor must not cost
    // the user their Wi-Fi credentials. Absent entirely => this is a pre-v3
    // config, so seed the list from the old fixed probe-target keys.
    c.monitorCount = 0;
    if (doc["monitors"].is<JsonArray>()) {
        for (JsonObjectConst m : doc["monitors"].as<JsonArrayConst>()) {
            if (c.monitorCount >= hw::MAX_MONITORS) break;
            String name = m["name"].is<const char *>() ? m["name"].as<String>() : String("");
            if (name.length() == 0) continue;
            MonitorType type;
            String typeName = m["type"].is<const char *>() ? m["type"].as<String>() : String("ping");
            if (!monitorTypeFromName(typeName, type)) continue;

            MonitorDef &d = c.monitors[c.monitorCount++];
            d.id = m["id"].is<const char *>() ? m["id"].as<String>() : String("");
            if (d.id.length() == 0) d.id = makeMonitorId();
            d.name = name;
            d.type = type;
            d.target = m["target"].is<const char *>() ? m["target"].as<String>() : String("");
            d.port = m["port"].is<int>() ? (uint16_t)m["port"].as<int>() : 0;
            d.latencyHighMs = m["latencyHighMs"].is<int>() ? (uint16_t)m["latencyHighMs"].as<int>() : 0;
            d.gen2Owned = m["gen2"].is<bool>() ? m["gen2"].as<bool>() : false;
        }
    } else {
        seedMonitorsFromLegacy(doc, c);
    }

    c.tempHighC = doc["tempHighC"] | c.tempHighC;
    c.tempLowC = doc["tempLowC"] | c.tempLowC;
    c.humidityHighPct = doc["humidityHighPct"] | c.humidityHighPct;
    c.humidityLowPct = doc["humidityLowPct"] | c.humidityLowPct;
    c.rssiLowDbm = doc["rssiLowDbm"] | c.rssiLowDbm;
    c.latencyHighMs = doc["latencyHighMs"] | c.latencyHighMs;
    c.packetLossHighPct = doc["packetLossHighPct"] | c.packetLossHighPct;

    c.gen2Enabled = doc["gen2Enabled"] | c.gen2Enabled;
    c.gen2ServerUrl = doc["gen2ServerUrl"] | c.gen2ServerUrl;
    c.gen2OrgId = doc["gen2OrgId"] | c.gen2OrgId;
    c.gen2LicenseKey = doc["gen2LicenseKey"] | c.gen2LicenseKey;
    c.gen2MonitorName = doc["gen2MonitorName"] | c.gen2MonitorName;
    c.gen2IntervalS = doc["gen2IntervalS"] | c.gen2IntervalS;
    c.gen2SyncEnabled = doc["gen2SyncEnabled"] | c.gen2SyncEnabled;
    c.gen2SyncIntervalS = doc["gen2SyncIntervalS"] | c.gen2SyncIntervalS;
    c.gen2PublishMonitors = doc["gen2PublishMonitors"] | c.gen2PublishMonitors;

    c.iotgwEnabled = doc["iotgwEnabled"] | c.iotgwEnabled;
    c.iotgwUrl = doc["iotgwUrl"] | c.iotgwUrl;
    c.iotgwToken = doc["iotgwToken"] | c.iotgwToken;
    c.iotgwIntervalS = doc["iotgwIntervalS"] | c.iotgwIntervalS;

    c.otaCheckEnabled = doc["otaCheckEnabled"] | c.otaCheckEnabled;
    c.otaCheckIntervalS = doc["otaCheckIntervalS"] | c.otaCheckIntervalS;

    config_ = c;
}

// One-time migration off the pre-v3 fixed five. Reads the legacy keys
// straight off the raw document because they no longer exist as DeviceConfig
// fields; once this has run, save() rewrites config.json without them.
void ConfigManager::seedMonitorsFromLegacy(JsonDocument &doc, DeviceConfig &c) {
    struct Seed {
        const char *key;
        const char *name;
        MonitorType type;
        const char *fallback;
    };
    // Same order, names and defaults the fixed probes had, so an upgraded
    // device looks unchanged to its user.
    static const Seed seeds[] = {
        {"gatewayTarget", "Gateway",        MonitorType::PING, ""},
        {"pingTarget1",   "Probe Target 1", MonitorType::PING, "8.8.8.8"},
        {"pingTarget2",   "Probe Target 2", MonitorType::PING, "1.1.1.1"},
        {"dnsDomain",     "DNS",            MonitorType::DNS,  "google.com"},
        {"httpTarget",    "HTTP/HTTPS",     MonitorType::HTTP, "https://example.com"},
    };

    bool fromLegacy = false;
    c.monitorCount = 0;
    for (const Seed &s : seeds) {
        if (c.monitorCount >= hw::MAX_MONITORS) break;
        bool present = doc[s.key].is<const char *>();
        if (present) fromLegacy = true;

        MonitorDef &d = c.monitors[c.monitorCount++];
        d.id = makeMonitorId();
        d.name = s.name;
        d.type = s.type;
        // A blank gateway target still means "use the DHCP-learned gateway".
        d.target = present ? doc[s.key].as<String>() : String(s.fallback);
        d.port = 0;
        d.gen2Owned = false;
    }
    Logger::info(TAG, "Seeded " + String(c.monitorCount) +
                          (fromLegacy ? " monitors from legacy probe targets" : " default monitors"));
}

void ConfigManager::toJson(JsonDocument &doc, bool redactSecrets) const {
    const DeviceConfig &c = config_;
    doc["configVersion"] = c.configVersion;
    doc["deviceName"] = c.deviceName;

    doc["wifiSsid"] = c.wifiSsid;
    if (!redactSecrets) doc["wifiPassword"] = c.wifiPassword;
    doc["wifiAuthMode"] = c.wifiAuthMode;
    doc["wifiUsername"] = c.wifiUsername;
    if (!redactSecrets) doc["wifiEapPassword"] = c.wifiEapPassword;
    doc["useStaticIp"] = c.useStaticIp;
    doc["staticIp"] = c.staticIp;
    doc["staticGateway"] = c.staticGateway;
    doc["staticSubnet"] = c.staticSubnet;
    doc["staticDns"] = c.staticDns;
    doc["wifiConnectAttempts"] = c.wifiConnectAttempts;

    doc["authUsername"] = c.authUsername;
    if (!redactSecrets) doc["authPassword"] = c.authPassword;

    doc["mdnsHostname"] = c.mdnsHostname;

    doc["sensorType"] = c.sensorType;
    doc["sensorGpio"] = c.sensorGpio;

    doc["environmentInterval"] = c.environmentIntervalS;
    doc["networkInterval"] = c.networkIntervalS;
    doc["dashboardRefresh"] = c.dashboardRefreshS;

    doc["probeTimeoutMs"] = c.probeTimeoutMs;
    doc["probePacketCount"] = c.probePacketCount;

    // Always emitted, including when empty: config.json is rewritten on every
    // boot, so an array that failed to round-trip would silently erase the
    // user's monitors.
    JsonArray mons = doc["monitors"].to<JsonArray>();
    for (uint8_t i = 0; i < c.monitorCount; i++) {
        const MonitorDef &m = c.monitors[i];
        JsonObject o = mons.add<JsonObject>();
        o["id"] = m.id;
        o["name"] = m.name;
        o["type"] = monitorTypeName(m.type);
        o["target"] = m.target;
        o["port"] = m.port;
        o["latencyHighMs"] = m.latencyHighMs; // 0 = inherit
        o["gen2"] = m.gen2Owned;
    }

    doc["tempHighC"] = c.tempHighC;
    doc["tempLowC"] = c.tempLowC;
    doc["humidityHighPct"] = c.humidityHighPct;
    doc["humidityLowPct"] = c.humidityLowPct;
    doc["rssiLowDbm"] = c.rssiLowDbm;
    doc["latencyHighMs"] = c.latencyHighMs;
    doc["packetLossHighPct"] = c.packetLossHighPct;

    doc["gen2Enabled"] = c.gen2Enabled;
    doc["gen2ServerUrl"] = c.gen2ServerUrl;
    doc["gen2OrgId"] = c.gen2OrgId;
    if (!redactSecrets) doc["gen2LicenseKey"] = c.gen2LicenseKey;
    doc["gen2MonitorName"] = c.gen2MonitorName;
    doc["gen2IntervalS"] = c.gen2IntervalS;
    doc["gen2SyncEnabled"] = c.gen2SyncEnabled;
    doc["gen2SyncIntervalS"] = c.gen2SyncIntervalS;
    doc["gen2PublishMonitors"] = c.gen2PublishMonitors;

    doc["iotgwEnabled"] = c.iotgwEnabled;
    doc["iotgwUrl"] = c.iotgwUrl;
    if (!redactSecrets) doc["iotgwToken"] = c.iotgwToken;
    doc["iotgwIntervalS"] = c.iotgwIntervalS;

    doc["otaCheckEnabled"] = c.otaCheckEnabled;
    doc["otaCheckIntervalS"] = c.otaCheckIntervalS;
}

bool ConfigManager::validate(const DeviceConfig &c, String &errorOut) const {
    // 32 bytes is a hard 802.11 protocol limit, not a preference — WiFi.begin()
    // rejects anything longer with a generic WL_CONNECT_FAILED and no
    // specific reason, so this catches it earlier with a clear message.
    // Empty is fine (means "not yet provisioned").
    if (c.wifiSsid.length() > 32) {
        errorOut = "wifiSsid too long (max 32 bytes — a hard Wi-Fi protocol limit, not a device-side restriction)";
        return false;
    }
    if (c.wifiConnectAttempts < 1 || c.wifiConnectAttempts > 10) {
        errorOut = "wifiConnectAttempts out of range (1-10)";
        return false;
    }
    if (c.wifiAuthMode != "personal" && c.wifiAuthMode != "enterprise") {
        errorOut = "wifiAuthMode must be personal or enterprise";
        return false;
    }
    if (c.wifiAuthMode == "enterprise" && c.wifiSsid.length() > 0 && c.wifiUsername.length() == 0) {
        errorOut = "wifiUsername is required when wifiAuthMode is enterprise";
        return false;
    }
    if (c.sensorType != "DHT11" && c.sensorType != "DHT22") {
        errorOut = "sensorType must be DHT11 or DHT22";
        return false;
    }
    if (c.environmentIntervalS < 2 || c.environmentIntervalS > 3600) {
        errorOut = "environmentInterval out of range (2-3600s)";
        return false;
    }
    if (c.networkIntervalS < 5 || c.networkIntervalS > 3600) {
        errorOut = "networkInterval out of range (5-3600s)";
        return false;
    }
    if (c.dashboardRefreshS < 1 || c.dashboardRefreshS > 300) {
        errorOut = "dashboardRefresh out of range (1-300s)";
        return false;
    }
    if (c.probePacketCount < 1 || c.probePacketCount > 20) {
        errorOut = "probePacketCount out of range (1-20)";
        return false;
    }
    if (c.authUsername.length() == 0) {
        errorOut = "authUsername cannot be empty";
        return false;
    }
    if (c.gen2IntervalS < 30 || c.gen2IntervalS > 3600) {
        errorOut = "gen2IntervalS out of range (30-3600s)";
        return false;
    }
    if (c.gen2ServerUrl.length() == 0) {
        errorOut = "gen2ServerUrl cannot be empty";
        return false;
    }
    if (c.gen2SyncIntervalS < 30 || c.gen2SyncIntervalS > 3600) {
        errorOut = "gen2SyncIntervalS out of range (30-3600s)";
        return false;
    }
    if (c.monitorCount > hw::MAX_MONITORS) {
        errorOut = "too many monitors (max " + String(hw::MAX_MONITORS) + " on this board)";
        return false;
    }
    for (uint8_t i = 0; i < c.monitorCount; i++) {
        const MonitorDef &m = c.monitors[i];
        if (m.name.length() == 0) {
            errorOut = "monitor name cannot be empty";
            return false;
        }
        // Names are the identity GEN2 dispatch matches on, so duplicates would
        // make a remove ambiguous.
        for (uint8_t j = i + 1; j < c.monitorCount; j++) {
            if (c.monitors[j].name == m.name) {
                errorOut = "duplicate monitor name: " + m.name;
                return false;
            }
        }
        // A blank PING target is meaningful — it means "the DHCP gateway",
        // which is how the old fixed Gateway probe behaved. Every other type
        // needs something concrete to act on.
        if (m.type != MonitorType::PING && m.target.length() == 0) {
            errorOut = "monitor \"" + m.name + "\" needs a target";
            return false;
        }
        if (m.type == MonitorType::PORT && m.port == 0) {
            errorOut = "monitor \"" + m.name + "\" needs a port (1-65535)";
            return false;
        }
        // 0 is the "inherit" sentinel; anything else is an explicit bar.
        if (m.latencyHighMs != 0 && m.latencyHighMs < 10) {
            errorOut = "monitor \"" + m.name + "\" latency threshold must be 0 (inherit) or at least 10ms";
            return false;
        }
    }
    if (c.iotgwIntervalS < 10 || c.iotgwIntervalS > 3600) {
        errorOut = "iotgwIntervalS out of range (10-3600s)";
        return false;
    }
    // URL/token are only required once the gateway is actually turned on, so
    // a device can keep a half-filled gateway section while it's disabled.
    if (c.iotgwEnabled) {
        if (!c.iotgwUrl.startsWith("http://") && !c.iotgwUrl.startsWith("https://")) {
            errorOut = "iotgwUrl must start with http:// or https:// when the IoT Gateway is enabled";
            return false;
        }
        if (c.iotgwToken.length() == 0) {
            errorOut = "iotgwToken is required when the IoT Gateway is enabled";
            return false;
        }
    }
    if (c.otaCheckIntervalS < 300 || c.otaCheckIntervalS > 604800) {
        errorOut = "otaCheckIntervalS out of range (300-604800s)";
        return false;
    }
    return true;
}

bool ConfigManager::update(JsonObjectConst updates, String &errorOut) {
    DeviceConfig c = config_;
    bool monitorsChanged = false;

    if (updates["deviceName"].is<const char *>()) c.deviceName = updates["deviceName"].as<String>();
    if (updates["wifiSsid"].is<const char *>()) c.wifiSsid = updates["wifiSsid"].as<String>();
    if (updates["wifiPassword"].is<const char *>()) c.wifiPassword = updates["wifiPassword"].as<String>();
    if (updates["wifiAuthMode"].is<const char *>()) c.wifiAuthMode = updates["wifiAuthMode"].as<String>();
    if (updates["wifiUsername"].is<const char *>()) c.wifiUsername = updates["wifiUsername"].as<String>();
    if (updates["wifiEapPassword"].is<const char *>() && updates["wifiEapPassword"].as<String>().length() > 0) {
        c.wifiEapPassword = updates["wifiEapPassword"].as<String>();
    }
    if (updates["useStaticIp"].is<bool>()) c.useStaticIp = updates["useStaticIp"];
    if (updates["staticIp"].is<const char *>()) c.staticIp = updates["staticIp"].as<String>();
    if (updates["staticGateway"].is<const char *>()) c.staticGateway = updates["staticGateway"].as<String>();
    if (updates["staticSubnet"].is<const char *>()) c.staticSubnet = updates["staticSubnet"].as<String>();
    if (updates["staticDns"].is<const char *>()) c.staticDns = updates["staticDns"].as<String>();
    if (updates["wifiConnectAttempts"].is<unsigned int>()) c.wifiConnectAttempts = updates["wifiConnectAttempts"];

    if (updates["authUsername"].is<const char *>()) c.authUsername = updates["authUsername"].as<String>();
    if (updates["authPassword"].is<const char *>() && updates["authPassword"].as<String>().length() > 0) {
        c.authPassword = updates["authPassword"].as<String>();
    }

    if (updates["mdnsHostname"].is<const char *>()) c.mdnsHostname = updates["mdnsHostname"].as<String>();

    if (updates["sensorType"].is<const char *>()) c.sensorType = updates["sensorType"].as<String>();
    if (updates["sensorGpio"].is<int>()) c.sensorGpio = updates["sensorGpio"];

    if (updates["environmentInterval"].is<unsigned int>()) c.environmentIntervalS = updates["environmentInterval"];
    if (updates["networkInterval"].is<unsigned int>()) c.networkIntervalS = updates["networkInterval"];
    if (updates["dashboardRefresh"].is<unsigned int>()) c.dashboardRefreshS = updates["dashboardRefresh"];

    if (updates["probeTimeoutMs"].is<unsigned int>()) c.probeTimeoutMs = updates["probeTimeoutMs"];
    if (updates["probePacketCount"].is<int>()) c.probePacketCount = updates["probePacketCount"];

    // Whole-list replace (there is no per-monitor PATCH: the Settings page
    // submits the complete list it rendered). GEN2-owned monitors are
    // re-attached from the CURRENT config regardless of what the client sent,
    // so they cannot be edited or deleted even by a client bypassing the UI —
    // the read-only rule lives here, not in the browser.
    if (updates["monitors"].is<JsonArrayConst>()) {
        // Compacted in place rather than via a second MonitorDef[MAX_MONITORS]
        // local: this runs inside the web server's call chain, where a few
        // hundred extra bytes of stack is a real cost on ESP8266. Safe because
        // the write index never outruns the read index.
        uint8_t n = 0;
        for (uint8_t i = 0; i < c.monitorCount; i++) {
            if (c.monitors[i].gen2Owned) {
                if (n != i) c.monitors[n] = c.monitors[i];
                n++;
            }
        }
        for (JsonObjectConst m : updates["monitors"].as<JsonArrayConst>()) {
            // A client-claimed gen2 flag is ignored outright; the real ones
            // were already carried over above.
            if (m["gen2"].is<bool>() && m["gen2"].as<bool>()) continue;
            if (n >= hw::MAX_MONITORS) {
                errorOut = "too many monitors (max " + String(hw::MAX_MONITORS) + " on this board)";
                return false;
            }
            String typeName = m["type"].is<const char *>() ? m["type"].as<String>() : String("ping");
            MonitorType type;
            if (!monitorTypeFromName(typeName, type)) {
                errorOut = "unknown monitor type: " + typeName;
                return false;
            }
            MonitorDef d;
            d.id = m["id"].is<const char *>() ? m["id"].as<String>() : String("");
            if (d.id.length() == 0) d.id = makeMonitorId();
            d.name = m["name"].is<const char *>() ? m["name"].as<String>() : String("");
            d.type = type;
            d.target = m["target"].is<const char *>() ? m["target"].as<String>() : String("");
            d.port = m["port"].is<int>() ? (uint16_t)m["port"].as<int>() : 0;
            d.latencyHighMs = m["latencyHighMs"].is<int>() ? (uint16_t)m["latencyHighMs"].as<int>() : 0;
            d.gen2Owned = false;
            c.monitors[n++] = d;
        }
        for (uint8_t i = n; i < hw::MAX_MONITORS; i++) c.monitors[i] = MonitorDef();
        c.monitorCount = n;
        monitorsChanged = true;
    }

    if (updates["tempHighC"].is<float>()) c.tempHighC = updates["tempHighC"];
    if (updates["tempLowC"].is<float>()) c.tempLowC = updates["tempLowC"];
    if (updates["humidityHighPct"].is<float>()) c.humidityHighPct = updates["humidityHighPct"];
    if (updates["humidityLowPct"].is<float>()) c.humidityLowPct = updates["humidityLowPct"];
    if (updates["rssiLowDbm"].is<int>()) c.rssiLowDbm = updates["rssiLowDbm"];
    if (updates["latencyHighMs"].is<float>()) c.latencyHighMs = updates["latencyHighMs"];
    if (updates["packetLossHighPct"].is<float>()) c.packetLossHighPct = updates["packetLossHighPct"];

    if (updates["gen2Enabled"].is<bool>()) c.gen2Enabled = updates["gen2Enabled"];
    if (updates["gen2ServerUrl"].is<const char *>() && updates["gen2ServerUrl"].as<String>().length() > 0) {
        c.gen2ServerUrl = updates["gen2ServerUrl"].as<String>();
    }
    if (updates["gen2OrgId"].is<const char *>()) c.gen2OrgId = updates["gen2OrgId"].as<String>();
    if (updates["gen2LicenseKey"].is<const char *>() && updates["gen2LicenseKey"].as<String>().length() > 0) {
        c.gen2LicenseKey = updates["gen2LicenseKey"].as<String>();
    }
    if (updates["gen2MonitorName"].is<const char *>()) c.gen2MonitorName = updates["gen2MonitorName"].as<String>();
    if (updates["gen2IntervalS"].is<unsigned int>()) c.gen2IntervalS = updates["gen2IntervalS"];
    if (updates["gen2SyncEnabled"].is<bool>()) c.gen2SyncEnabled = updates["gen2SyncEnabled"];
    if (updates["gen2SyncIntervalS"].is<unsigned int>()) c.gen2SyncIntervalS = updates["gen2SyncIntervalS"];
    if (updates["gen2PublishMonitors"].is<bool>()) c.gen2PublishMonitors = updates["gen2PublishMonitors"];

    if (updates["iotgwEnabled"].is<bool>()) c.iotgwEnabled = updates["iotgwEnabled"];
    if (updates["iotgwUrl"].is<const char *>()) c.iotgwUrl = updates["iotgwUrl"].as<String>();
    if (updates["iotgwToken"].is<const char *>() && updates["iotgwToken"].as<String>().length() > 0) {
        c.iotgwToken = updates["iotgwToken"].as<String>();
    }
    if (updates["iotgwIntervalS"].is<unsigned int>()) c.iotgwIntervalS = updates["iotgwIntervalS"];

    if (updates["otaCheckEnabled"].is<bool>()) c.otaCheckEnabled = updates["otaCheckEnabled"];
    if (updates["otaCheckIntervalS"].is<unsigned int>()) c.otaCheckIntervalS = updates["otaCheckIntervalS"];

    if (!validate(c, errorOut)) {
        return false;
    }

    config_ = c;
    if (monitorsChanged) monitorsGeneration_++;
    return save();
}

bool ConfigManager::addOrUpdateMonitor(const MonitorDef &m, String &errorOut) {
    DeviceConfig c = config_;

    int existing = -1;
    for (uint8_t i = 0; i < c.monitorCount; i++) {
        if (c.monitors[i].name == m.name) {
            existing = i;
            break;
        }
    }

    if (existing >= 0) {
        // Re-dispatching an existing monitor updates it in place rather than
        // duplicating it, so replayed jobs are idempotent. The original id is
        // kept so live results stay attached to it.
        String keepId = c.monitors[existing].id;
        c.monitors[existing] = m;
        if (c.monitors[existing].id.length() == 0) c.monitors[existing].id = keepId;
    } else {
        if (c.monitorCount >= hw::MAX_MONITORS) {
            errorOut = "monitor list is full (max " + String(hw::MAX_MONITORS) + " on this board)";
            return false;
        }
        c.monitors[c.monitorCount] = m;
        if (c.monitors[c.monitorCount].id.length() == 0) {
            c.monitors[c.monitorCount].id = makeMonitorId();
        }
        c.monitorCount++;
    }

    if (!validate(c, errorOut)) return false;
    config_ = c;
    monitorsGeneration_++;
    return save();
}

bool ConfigManager::removeMonitorByName(const String &name) {
    DeviceConfig c = config_;
    uint8_t n = 0;
    bool removed = false;
    for (uint8_t i = 0; i < c.monitorCount; i++) {
        if (c.monitors[i].name == name) {
            removed = true;
            continue;
        }
        if (n != i) c.monitors[n] = c.monitors[i];
        n++;
    }
    if (!removed) return false; // nothing to do — treat as already applied

    for (uint8_t i = n; i < c.monitorCount; i++) c.monitors[i] = MonitorDef();
    c.monitorCount = n;
    config_ = c;
    monitorsGeneration_++;
    return save();
}

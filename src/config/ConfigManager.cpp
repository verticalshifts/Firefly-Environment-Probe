#include "ConfigManager.h"
#include "hardware/HardwareConfig.h"
#include "util/Logger.h"

static const char *TAG = "Config";

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

    c.gatewayTarget = doc["gatewayTarget"] | c.gatewayTarget;
    c.pingTarget1 = doc["pingTarget1"] | c.pingTarget1;
    c.pingTarget2 = doc["pingTarget2"] | c.pingTarget2;
    c.dnsDomain = doc["dnsDomain"] | c.dnsDomain;
    c.httpTarget = doc["httpTarget"] | c.httpTarget;
    c.probeTimeoutMs = doc["probeTimeoutMs"] | c.probeTimeoutMs;
    c.probePacketCount = doc["probePacketCount"] | c.probePacketCount;

    c.tempHighC = doc["tempHighC"] | c.tempHighC;
    c.tempLowC = doc["tempLowC"] | c.tempLowC;
    c.humidityHighPct = doc["humidityHighPct"] | c.humidityHighPct;
    c.humidityLowPct = doc["humidityLowPct"] | c.humidityLowPct;
    c.rssiLowDbm = doc["rssiLowDbm"] | c.rssiLowDbm;
    c.latencyHighMs = doc["latencyHighMs"] | c.latencyHighMs;
    c.packetLossHighPct = doc["packetLossHighPct"] | c.packetLossHighPct;

    config_ = c;
}

void ConfigManager::toJson(JsonDocument &doc, bool redactSecrets) const {
    const DeviceConfig &c = config_;
    doc["configVersion"] = c.configVersion;
    doc["deviceName"] = c.deviceName;

    doc["wifiSsid"] = c.wifiSsid;
    if (!redactSecrets) doc["wifiPassword"] = c.wifiPassword;
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

    doc["gatewayTarget"] = c.gatewayTarget;
    doc["pingTarget1"] = c.pingTarget1;
    doc["pingTarget2"] = c.pingTarget2;
    doc["dnsDomain"] = c.dnsDomain;
    doc["httpTarget"] = c.httpTarget;
    doc["probeTimeoutMs"] = c.probeTimeoutMs;
    doc["probePacketCount"] = c.probePacketCount;

    doc["tempHighC"] = c.tempHighC;
    doc["tempLowC"] = c.tempLowC;
    doc["humidityHighPct"] = c.humidityHighPct;
    doc["humidityLowPct"] = c.humidityLowPct;
    doc["rssiLowDbm"] = c.rssiLowDbm;
    doc["latencyHighMs"] = c.latencyHighMs;
    doc["packetLossHighPct"] = c.packetLossHighPct;
}

bool ConfigManager::validate(const DeviceConfig &c, String &errorOut) const {
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
    return true;
}

bool ConfigManager::update(JsonObjectConst updates, String &errorOut) {
    DeviceConfig c = config_;

    if (updates["deviceName"].is<const char *>()) c.deviceName = updates["deviceName"].as<String>();
    if (updates["wifiSsid"].is<const char *>()) c.wifiSsid = updates["wifiSsid"].as<String>();
    if (updates["wifiPassword"].is<const char *>()) c.wifiPassword = updates["wifiPassword"].as<String>();
    if (updates["useStaticIp"].is<bool>()) c.useStaticIp = updates["useStaticIp"];
    if (updates["staticIp"].is<const char *>()) c.staticIp = updates["staticIp"].as<String>();
    if (updates["staticGateway"].is<const char *>()) c.staticGateway = updates["staticGateway"].as<String>();
    if (updates["staticSubnet"].is<const char *>()) c.staticSubnet = updates["staticSubnet"].as<String>();
    if (updates["staticDns"].is<const char *>()) c.staticDns = updates["staticDns"].as<String>();

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

    if (updates["gatewayTarget"].is<const char *>()) c.gatewayTarget = updates["gatewayTarget"].as<String>();
    if (updates["pingTarget1"].is<const char *>()) c.pingTarget1 = updates["pingTarget1"].as<String>();
    if (updates["pingTarget2"].is<const char *>()) c.pingTarget2 = updates["pingTarget2"].as<String>();
    if (updates["dnsDomain"].is<const char *>()) c.dnsDomain = updates["dnsDomain"].as<String>();
    if (updates["httpTarget"].is<const char *>()) c.httpTarget = updates["httpTarget"].as<String>();
    if (updates["probeTimeoutMs"].is<unsigned int>()) c.probeTimeoutMs = updates["probeTimeoutMs"];
    if (updates["probePacketCount"].is<int>()) c.probePacketCount = updates["probePacketCount"];

    if (updates["tempHighC"].is<float>()) c.tempHighC = updates["tempHighC"];
    if (updates["tempLowC"].is<float>()) c.tempLowC = updates["tempLowC"];
    if (updates["humidityHighPct"].is<float>()) c.humidityHighPct = updates["humidityHighPct"];
    if (updates["humidityLowPct"].is<float>()) c.humidityLowPct = updates["humidityLowPct"];
    if (updates["rssiLowDbm"].is<int>()) c.rssiLowDbm = updates["rssiLowDbm"];
    if (updates["latencyHighMs"].is<float>()) c.latencyHighMs = updates["latencyHighMs"];
    if (updates["packetLossHighPct"].is<float>()) c.packetLossHighPct = updates["packetLossHighPct"];

    if (!validate(c, errorOut)) {
        return false;
    }

    config_ = c;
    return save();
}

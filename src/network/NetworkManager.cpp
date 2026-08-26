#include "NetworkManager.h"
#include "hardware/HardwareConfig.h"
#include "util/Logger.h"

#if defined(PLATFORM_ESP32)
#include <ESPmDNS.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266mDNS.h>
#endif

static const char *TAG = "Network";
static const byte DNS_PORT = 53;

NetworkManager::NetworkManager(ConfigManager &config) : config_(config) {}

String NetworkManager::deriveApSsid() {
    if (strlen(hw::AP_SSID_OVERRIDE) > 0) {
        return String(hw::AP_SSID_OVERRIDE);
    }

#if defined(PLATFORM_ESP32)
    uint64_t mac = ESP.getEfuseMac();
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X", (unsigned int)(mac & 0xFFFFFF));
#elif defined(PLATFORM_ESP8266)
    char suffix[7];
    snprintf(suffix, sizeof(suffix), "%06X", ESP.getChipId() & 0xFFFFFF);
#endif
    return String("ENVPROBE-") + suffix;
}

String NetworkManager::deriveMdnsHostname() {
    const DeviceConfig &c = config_.get();
    if (c.mdnsHostname.length() > 0) return c.mdnsHostname;

    String slug = c.deviceName;
    slug.toLowerCase();
    for (size_t i = 0; i < slug.length(); i++) {
        char ch = slug[i];
        if (!isalnum((unsigned char)ch)) slug.setCharAt(i, '-');
    }
    if (slug.length() == 0) slug = "envprobe";
    return slug;
}

void NetworkManager::begin() {
    apSsid_ = deriveApSsid();

    if (config_.isProvisioned()) {
        connectSTA(/*blockingFirstAttempt=*/true);
        if (WiFi.status() != WL_CONNECTED) {
            Logger::warn(TAG, "Could not join configured Wi-Fi, falling back to provisioning AP");
            startProvisioningAP();
        }
    } else {
        Logger::info(TAG, "No Wi-Fi configured, starting provisioning AP");
        startProvisioningAP();
    }
}

void NetworkManager::connectSTA(bool blockingFirstAttempt) {
    const DeviceConfig &c = config_.get();
    mode_ = NetworkMode::STATION;
    WiFi.mode(WIFI_STA);

    if (c.useStaticIp && c.staticIp.length() > 0) {
        IPAddress ip, gw, sn, dns;
        ip.fromString(c.staticIp);
        gw.fromString(c.staticGateway);
        sn.fromString(c.staticSubnet);
        dns.fromString(c.staticDns.length() ? c.staticDns : c.staticGateway);
        WiFi.config(ip, gw, sn, dns);
    }

    WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str());
    Logger::info(TAG, "Connecting to " + c.wifiSsid);

    if (blockingFirstAttempt) {
        uint8_t attempts = c.wifiConnectAttempts > 0 ? c.wifiConnectAttempts : 20;
        for (uint8_t i = 0; i < attempts && WiFi.status() != WL_CONNECTED; i++) {
            delay(500); // acceptable one-time blocking wait during setup(), not loop()
        }
        if (WiFi.status() == WL_CONNECTED) {
            everConnected_ = true;
            Logger::info(TAG, "Connected, IP " + WiFi.localIP().toString());
            setupMDNS();
        }
    }
    lastConnectAttemptMs_ = millis();
}

void NetworkManager::startProvisioningAP() {
    if (mode_ == NetworkMode::PROVISIONING_AP && WiFi.getMode() == WIFI_AP) return; // already active

    mode_ = NetworkMode::PROVISIONING_AP;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid_.c_str());

    IPAddress apIP = WiFi.softAPIP();
    dnsServer_.start(DNS_PORT, "*", apIP); // captive-portal-style DNS redirect

    Logger::info(TAG, "Provisioning AP started: " + apSsid_ + " (" + apIP.toString() + ")");
}

void NetworkManager::applyNewCredentials() {
    Logger::info(TAG, "Applying new Wi-Fi credentials");
    dnsServer_.stop();
    connectSTA(/*blockingFirstAttempt=*/true);
    if (WiFi.status() != WL_CONNECTED) {
        Logger::warn(TAG, "New credentials failed to connect, returning to provisioning AP");
        startProvisioningAP();
    }
}

void NetworkManager::loop() {
    if (mode_ == NetworkMode::PROVISIONING_AP) {
        dnsServer_.processNextRequest();
        return;
    }

#if defined(PLATFORM_ESP32)
    // ESPmDNS needs no periodic pump.
#elif defined(PLATFORM_ESP8266)
    MDNS.update();
#endif

    if (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        // Backoff: retry every 5s rather than hammering WiFi.reconnect().
        if (now - lastConnectAttemptMs_ >= 5000) {
            lastConnectAttemptMs_ = now;
            Logger::warn(TAG, "Wi-Fi disconnected, attempting reconnect");
            WiFi.reconnect();
            reconnectCount_++;
        }
    } else if (!everConnected_) {
        everConnected_ = true;
        setupMDNS();
    }
}

void NetworkManager::setupMDNS() {
    String host = deriveMdnsHostname();
    if (MDNS.begin(host.c_str())) {
        MDNS.addService("http", "tcp", hw::HTTP_PORT);
        Logger::info(TAG, "mDNS responder started: " + host + ".local");
    } else {
        Logger::warn(TAG, "mDNS responder failed to start");
    }
}

bool NetworkManager::isConnected() {
    return mode_ == NetworkMode::STATION && WiFi.status() == WL_CONNECTED;
}

String NetworkManager::getSSID() {
    return isConnected() ? WiFi.SSID() : String("");
}

String NetworkManager::getIPAddress() {
    if (mode_ == NetworkMode::PROVISIONING_AP) return WiFi.softAPIP().toString();
    return isConnected() ? WiFi.localIP().toString() : String("0.0.0.0");
}

String NetworkManager::getGatewayIP() {
    return isConnected() ? WiFi.gatewayIP().toString() : String("");
}

int NetworkManager::getRSSI() {
    return isConnected() ? WiFi.RSSI() : 0;
}

int32_t NetworkManager::getChannel() {
    return isConnected() ? WiFi.channel() : 0;
}

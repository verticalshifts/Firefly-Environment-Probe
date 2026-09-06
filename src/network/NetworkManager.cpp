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

// Human-readable WiFi.status() for the "could not join" log line — the bare
// WARN gave no signal on *why* (wrong password vs. SSID not found/out of
// range vs. something else), which is exactly the ambiguity that made a
// real dual-band-router join failure hard to diagnose remotely.
static const char *wifiStatusString(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "idle (never got past association)";
        case WL_NO_SSID_AVAIL: return "SSID not found — check it's in range and spelled exactly right, and (for a dual-band router) that this device's 2.4GHz-only radio can actually see it";
        case WL_CONNECT_FAILED: return "connect failed — likely wrong password";
        case WL_CONNECTION_LOST: return "connection lost mid-handshake";
        case WL_DISCONNECTED: return "disconnected";
#if defined(PLATFORM_ESP8266)
        case WL_WRONG_PASSWORD: return "wrong password"; // ESP8266-only status code, not in ESP32's wl_status_t
#endif
        default: return "unknown";
    }
}

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
            Logger::warn(TAG, "Could not join configured Wi-Fi (" + String(wifiStatusString(WiFi.status())) +
                                   "), falling back to provisioning AP");
            startProvisioningAP();
        }
    } else {
        Logger::info(TAG, "No Wi-Fi configured, starting provisioning AP");
        startProvisioningAP();
    }
}

// Total wall-clock budget for all of connectSTA()'s blocking attempts put
// together, before giving up and falling back to the provisioning AP.
// c.wifiConnectAttempts (default 3) splits this evenly — e.g. 3 attempts
// in this 60s window, each a fresh scan + targeted WiFi.begin(), not one
// attempt polled repeatedly (the old behavior). A network with a real but
// marginal/intermittent signal, or a controller that needs a moment to
// admit a new client, is far more likely to succeed on a *retried* attempt
// than a single long wait for the same attempt to somehow resolve itself.
static constexpr uint32_t WIFI_CONNECT_TOTAL_BUDGET_MS = 60000;

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

#if defined(PLATFORM_ESP32)
    // ESP32 refuses to associate with anything weaker than WPA2-PSK by
    // *policy*, independent of what the AP actually offers (default
    // _minSecurity = WIFI_AUTH_WPA2_PSK) — confirmed in WiFiSTA.cpp. That
    // silently rules out open and WEP networks even though the AP itself
    // may be perfectly joinable. Lower the floor to OPEN so the actual
    // negotiated security is whatever the AP and the supplied
    // password/absence-of-password imply, not an extra app-side gate.
    WiFi.setMinSecurity(WIFI_AUTH_OPEN);
#endif

    if (blockingFirstAttempt) {
        uint8_t maxAttempts = c.wifiConnectAttempts > 0 ? c.wifiConnectAttempts : 3;
        uint32_t perAttemptMs = WIFI_CONNECT_TOTAL_BUDGET_MS / maxAttempts;
        for (uint8_t attempt = 1; attempt <= maxAttempts; attempt++) {
            Logger::info(TAG, "Wi-Fi connect attempt " + String(attempt) + "/" + String(maxAttempts));
            if (performConnectAttempt(c, perAttemptMs)) break;
        }
        if (WiFi.status() == WL_CONNECTED) {
            everConnected_ = true;
            Logger::info(TAG, "Connected, IP " + WiFi.localIP().toString());
            setupMDNS();
        }
    } else {
        // Non-blocking path: single begin(), no wait, no scan — matches the
        // pre-existing behavior for this (currently unused) branch.
        WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str());
        Logger::info(TAG, "Connecting to " + c.wifiSsid);
    }
    lastConnectAttemptMs_ = millis();
}

bool NetworkManager::performConnectAttempt(const DeviceConfig &c, uint32_t maxWaitMs) {
    bool haveBestBssid = false;
    uint8_t bestBssid[6] = {0};
    int32_t bestChannel = 0;

    // Scan before every attempt, not just the first — signal/AP conditions
    // (RSSI, which BSSID answers, whether a network is WEP) can genuinely
    // change between retries, and this is the only way to react to that.
    // show_hidden=true so a cloaked-SSID network at least has a chance of
    // showing up here too (WiFi.begin() below connects by name regardless
    // of scan visibility, so a hidden network that doesn't show up here
    // can still succeed — this is diagnostics/targeting, not a gate).
    int found = WiFi.scanNetworks(false, true);
    int matches = 0;
    int bestRssi = -1000;
#if defined(PLATFORM_ESP8266)
    bool targetIsWep = false;
#endif
    for (int i = 0; i < found; i++) {
        if (WiFi.SSID(i) != c.wifiSsid) continue;
        matches++;
        Logger::info(TAG, "  scan: " + WiFi.SSID(i) + " bssid=" + WiFi.BSSIDstr(i) +
                               " ch=" + String(WiFi.channel(i)) +
                               " rssi=" + String(WiFi.RSSI(i)) + "dBm" +
                               " encType=" + String(WiFi.encryptionType(i)));
#if defined(PLATFORM_ESP8266)
        if (WiFi.encryptionType(i) == ENC_TYPE_WEP) targetIsWep = true;
#endif
        // Track the strongest BSSID seen for this SSID — left to its own
        // devices, WiFi.begin(ssid, pass) doesn't guarantee picking the
        // best AP in a multi-AP (controller-based) deployment; explicitly
        // targeting the strongest one is a real reliability improvement
        // there, confirmed live: this exact scan found a second AP ~10dB
        // stronger than the one a bare SSID-only connect had been using.
        if (WiFi.RSSI(i) > bestRssi) {
            bestRssi = WiFi.RSSI(i);
            bestChannel = WiFi.channel(i);
            memcpy(bestBssid, WiFi.BSSID(i), 6);
            haveBestBssid = true;
        }
    }
    if (matches == 0) {
        Logger::warn(TAG, "Pre-connect scan found 0 APs broadcasting \"" + c.wifiSsid +
                               "\" (out of " + String(found) + " networks seen) — could still be hidden; "
                               "not necessarily just an auth/DHCP problem");
    } else {
        Logger::info(TAG, "Pre-connect scan: " + String(matches) + " AP(s) broadcasting \"" + c.wifiSsid +
                               "\", strongest at " + String(bestRssi) + "dBm");
    }
    WiFi.scanDelete();

#if defined(PLATFORM_ESP8266)
    // ESP8266's WiFi.begin() always frames a non-empty password as WPA-PSK
    // unless this is explicitly enabled first (confirmed in
    // ESP8266WiFiSTA.cpp: authmode is AUTH_WPA_PSK for any non-empty
    // password unless _useInsecureWEP is set) — so a WEP-only network would
    // silently fail to associate at all without this. Re-set on every
    // attempt (not just once) so a network that used to be WEP and has
    // since moved to WPA doesn't leave stale state behind.
    WiFi.enableInsecureWEP(targetIsWep);
    if (targetIsWep) Logger::info(TAG, "Target network is WEP-secured — using legacy WEP auth");
#endif

    if (haveBestBssid) {
        WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str(), bestChannel, bestBssid, true);
        Logger::info(TAG, "Connecting to " + c.wifiSsid + " (targeting strongest AP, ch=" + String(bestChannel) + ")");
    } else {
        WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str());
        Logger::info(TAG, "Connecting to " + c.wifiSsid);
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < maxWaitMs) {
        delay(500); // acceptable one-time blocking wait during setup(), not loop()
    }
    return WiFi.status() == WL_CONNECTED;
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

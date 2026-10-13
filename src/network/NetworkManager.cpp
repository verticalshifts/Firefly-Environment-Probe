#include "NetworkManager.h"
#include "hardware/HardwareConfig.h"
#include "hardware/Platform.h"
#include "util/Logger.h"

#if defined(PLATFORM_ESP32)
#include <ESPmDNS.h>
// esp_wifi_sta_wpa2_ent_disable(); WPA2_AUTH_PEAP and the WiFi.begin(ssid,
// method, identity, username, password, ...) overload used below already
// come from <WiFi.h> (WiFiSTA.h includes esp_wpa2.h internally) — included
// explicitly here just to document the dependency.
#include <esp_wpa2.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266mDNS.h>
// No Arduino-level WPA2-Enterprise API exists for ESP8266 (unlike ESP32's
// esp_wpa2.h wrapper above) — these are raw NONOS SDK C functions, not part
// of any documented/supported Arduino-ESP8266 surface. This is a known
// community DIY pattern, not an Espressif-supported one: re-verify the
// call sequence below on real hardware after any ESP8266 platform/core
// version bump in platformio.ini.
extern "C" {
#include <user_interface.h>
#include <wpa2_enterprise.h>
}
#endif

static const char *TAG = "Network";
static const byte DNS_PORT = 53;

// ---------------------------------------------------------------------------
// Disconnect reasons. The Wi-Fi stack reports WHY a link dropped, but only
// through an event — WiFi.status() just says "not connected". Without this
// a weak signal, an AP that kicked us and a rejected password all produce
// the same log line, which is why random drops were undiagnosable.
//
// The callback runs on the Wi-Fi/event task (ESP32) or from the SDK
// (ESP8266), so it only stores a byte and sets a flag; loop() does the
// logging on the main task.
// ---------------------------------------------------------------------------
static volatile uint8_t g_disconnectReason = 0;
static volatile bool g_disconnectPending = false;

// Reason 8 ("station is leaving") is what our own disconnect-before-scan
// produces. Recording it would overwrite the reason that actually matters.
static constexpr uint8_t REASON_SELF_LEAVE = 8;

static void noteDisconnect(uint8_t reason) {
    if (reason == REASON_SELF_LEAVE) return;
    g_disconnectReason = reason;
    g_disconnectPending = true;
}

#if defined(PLATFORM_ESP8266)
static WiFiEventHandler g_disconnectHandler; // must outlive the subscription
#endif

static void registerWifiEvents() {
    static bool registered = false;
    if (registered) return;
    registered = true;
#if defined(PLATFORM_ESP32)
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        noteDisconnect(info.wifi_sta_disconnected.reason);
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#elif defined(PLATFORM_ESP8266)
    g_disconnectHandler = WiFi.onStationModeDisconnected(
        [](const WiFiEventStationModeDisconnected &e) { noteDisconnect((uint8_t)e.reason); });
#endif
}

// The codes below 200 are standard 802.11 reason codes; 200+ are Espressif's
// own, shared by both platforms.
const char *NetworkManager::disconnectReasonText(uint8_t reason) {
    switch (reason) {
        case 0:   return "none";
        case 1:   return "unspecified";
        case 2:   return "authentication expired";
        case 3:   return "AP deauthenticated us (it is leaving or restarting)";
        case 4:   return "AP dropped us for inactivity";
        case 5:   return "AP is full";
        case 6:
        case 7:   return "AP no longer recognises us (it restarted or lost our session)";
        case 8:   return "we left";
        case 15:  return "4-way handshake timeout (wrong password, or a very weak link)";
        case 16:  return "group key update timeout";
        case 200: return "beacon timeout (AP no longer heard: range, interference or AP down)";
        case 201: return "no AP found with this SSID";
        case 202: return "authentication failed";
        case 203: return "association failed";
        case 204: return "handshake timeout";
        case 205: return "connection failed";
        default:  return "other (see the 802.11 / Espressif reason code)";
    }
}

// Applied whenever station mode is (re)entered.
static void applyStationPolicy() {
    // One owner for reconnection. The core's own auto-reconnect and loop()'s
    // attempts would otherwise both issue connects and cancel each other's.
    WiFi.setAutoReconnect(false);

    // No Wi-Fi power-save. Modem-sleep makes the radio doze between beacons,
    // which is a known source of dropped links and slow responses with some
    // access points. This device is mains/USB powered, so the saving is
    // worth nothing here. Not proven to be the cause of any drop seen so far.
#if defined(PLATFORM_ESP32)
    WiFi.setSleep(false);
#elif defined(PLATFORM_ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

}

// Human-readable WiFi.status() for the "could not join" log line — the bare
// WARN gave no signal on *why* (wrong password vs. SSID not found/out of
// range vs. something else), which is exactly the ambiguity that made a
// real dual-band-router join failure hard to diagnose remotely.
static const char *wifiStatusString(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "idle (never got past association)";
        case WL_NO_SSID_AVAIL: return "SSID not found — check it's in range and spelled exactly right, and (for a dual-band router) that this device's 2.4GHz-only radio can actually see it";
        case WL_CONNECT_FAILED: return "connect failed";
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
    registerWifiEvents();

    if (config_.isProvisioned()) {
        connectSTA(/*blockingFirstAttempt=*/true);
        if (WiFi.status() != WL_CONNECTED) {
            // The reason code is what the stack actually reported; the status
            // text alone guessed "likely wrong password" for what was really
            // an association failure on a marginal link.
            Logger::warn(TAG, "Could not join configured Wi-Fi (" + String(wifiStatusString(WiFi.status())) +
                                   "; last reason " + String((uint8_t)g_disconnectReason) + ": " +
                                   disconnectReasonText(g_disconnectReason) +
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
    applyStationPolicy();
    applyTxPower();

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
        // pre-existing behavior for this (currently unused) branch. Personal-
        // auth only; not wired for wifiAuthMode=="enterprise" since nothing
        // calls this branch today.
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
    //
    // A scan cannot run while the station is still trying to connect — the
    // driver refuses it and scanNetworks() returns -2 (WIFI_SCAN_FAILED).
    // That happened on every attempt after the first, and on every "re-scan"
    // made from loop(), so those attempts never actually scanned: the log
    // just said "found 0 APs (out of -2 networks seen)". Stop the pending
    // connect first.
    if (WiFi.status() != WL_CONNECTED) {
#if defined(PLATFORM_ESP32)
        WiFi.disconnect(false, false);
#elif defined(PLATFORM_ESP8266)
        // Raw SDK call: ESP8266's WiFi.disconnect() also blanks the stored
        // SSID/password, which a later WiFi.reconnect() depends on.
        wifi_station_disconnect();
#endif
        delay(100);
    }

    // Skip the scan for a network that has shown itself to be hidden — see
    // unseenScans_ in the header.
    bool skipScan = unseenScans_ >= 2 && (++scanSkipCounter_ % 5 != 0);

    int found = 0;
    bool scanOk = false;
    if (skipScan) {
        Logger::info(TAG, "SSID not visible to scans (hidden?) — joining by name");
    } else {
        found = WiFi.scanNetworks(false, true);
        scanOk = found >= 0;
        if (!scanOk) {
            Logger::warn(TAG, "Wi-Fi scan failed (code " + String(found) +
                                  ") — connecting by name without targeting an AP");
            found = 0;
        }
        // A scan measured ~6.3s on a band with 30 networks. This function
        // also runs from loop() with the watchdog armed, so it feeds it at
        // each bounded stage rather than relying on the total staying short.
        PlatformManager::feedWatchdog();
    }
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
    if (scanOk) {
        if (matches == 0) { if (unseenScans_ < 255) unseenScans_++; }
        else unseenScans_ = 0;
    }

    if (!scanOk) {
        // Skipped or failed, and already reported; "0 APs seen" would be false.
    } else if (matches == 0) {
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

    if (c.wifiAuthMode == "enterprise") {
        Logger::info(TAG, "Connecting to " + c.wifiSsid + " (WPA2-Enterprise)" +
                               (haveBestBssid ? " (targeting strongest AP, ch=" + String(bestChannel) + ")" : ""));
#if defined(PLATFORM_ESP32)
        // Built-in Arduino-ESP32 overload (WiFiSTA.h) — internally calls
        // esp_wifi_sta_wpa2_ent_set_identity/username/password + _enable()
        // then a plain WiFi.begin(ssid). wpa2_identity and wpa2_username
        // both get wifiUsername (one field, both roles — product decision).
        // Only PEAP is supported (not TLS/TTLS), and ca_pem/client_crt/
        // client_key are NULL — no RADIUS server certificate validation;
        // see docs/architecture.md's Auth model for that trade-off.
        WiFi.begin(c.wifiSsid.c_str(), WPA2_AUTH_PEAP,
                   c.wifiUsername.c_str(), c.wifiUsername.c_str(), c.wifiEapPassword.c_str(),
                   NULL, NULL, NULL,
                   haveBestBssid ? bestChannel : 0, haveBestBssid ? bestBssid : nullptr, true);
#elif defined(PLATFORM_ESP8266)
        // No Arduino-level Enterprise API on ESP8266 — drive the raw NONOS
        // SDK station config + WPA2-Enterprise C API directly, bypassing
        // WiFi.begin()/ESP8266WiFiSTAClass entirely for this connect.
        // Unofficial community pattern (see the include-block comment
        // above); the exact call ordering below is the most common DIY
        // sequence found, NOT confirmed against real hardware in this
        // session (no ESP8266 board attached) — re-verify on real hardware
        // before relying on it.
        wifi_station_disconnect();

        struct station_config conf;
        memset(&conf, 0, sizeof(conf)); // password[] stays zeroed — unused for enterprise auth
        strncpy((char *)conf.ssid, c.wifiSsid.c_str(), sizeof(conf.ssid));
        if (haveBestBssid) {
            conf.bssid_set = 1;
            memcpy(conf.bssid, bestBssid, 6);
        }
        wifi_station_set_config_current(&conf); // RAM-only — ConfigManager/LittleFS already
                                                 // owns durable persistence of this data

        wifi_station_set_wpa2_enterprise_auth(1);
        wifi_station_set_enterprise_identity((u8 *)c.wifiUsername.c_str(), c.wifiUsername.length());
        wifi_station_set_enterprise_username((u8 *)c.wifiUsername.c_str(), c.wifiUsername.length());
        wifi_station_set_enterprise_password((u8 *)c.wifiEapPassword.c_str(), c.wifiEapPassword.length());

        wifi_station_connect();
#endif
    } else {
#if defined(PLATFORM_ESP32)
        // Disable any previously-enabled Enterprise state before a personal
        // connect — the stock core's personal-mode begin() never does this
        // itself (confirmed by reading WiFiSTA.cpp), so a device that was
        // ever switched to Enterprise and back to Personal could otherwise
        // get stuck. Cheap/harmless to call unconditionally every attempt.
        esp_wifi_sta_wpa2_ent_disable();
#elif defined(PLATFORM_ESP8266)
        // Same rationale as the ESP32 branch above: clear any Enterprise
        // state left over from a prior "enterprise" attempt before falling
        // back to a plain WiFi.begin() with a PSK.
        wifi_station_set_wpa2_enterprise_auth(0);
        wifi_station_clear_enterprise_identity();
        wifi_station_clear_enterprise_username();
        wifi_station_clear_enterprise_password();
#endif
        if (haveBestBssid) {
            WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str(), bestChannel, bestBssid, true);
            Logger::info(TAG, "Connecting to " + c.wifiSsid + " (targeting strongest AP, ch=" + String(bestChannel) + ")");
        } else {
            WiFi.begin(c.wifiSsid.c_str(), c.wifiPassword.c_str());
            Logger::info(TAG, "Connecting to " + c.wifiSsid);
        }
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < maxWaitMs) {
        delay(500);
        PlatformManager::feedWatchdog(); // bounded by maxWaitMs; see the note after the scan
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

void NetworkManager::applyTxPower() {
#if defined(PLATFORM_ESP32)
    // Per-device setting, 15dBm by default. A live A/B on the bench
    // board (levels alternated minute by minute) showed no link-quality
    // difference between 7 and 18dBm, so lowering it is not a fix for drops;
    // it is kept as a knob for boards with a weak supply. The radio rounds
    // down to the step it supports (19 -> 18, 11 -> 9.5, 8 -> 7 as read back).
    // Needs the radio started (after WiFi.mode()).
    uint8_t dbm = config_.get().wifiTxPowerDbm;
    if (dbm == 0) dbm = hw::WIFI_TX_POWER_DEFAULT_DBM;
    wifi_power_t level = WIFI_POWER_19_5dBm;
    if (dbm <= 8) level = WIFI_POWER_8_5dBm;
    else if (dbm <= 11) level = WIFI_POWER_11dBm;
    else if (dbm <= 13) level = WIFI_POWER_13dBm;
    else if (dbm <= 15) level = WIFI_POWER_15dBm;
    else if (dbm <= 17) level = WIFI_POWER_17dBm;
    const bool ok = WiFi.setTxPower(level);
    Logger::info(TAG, String("TX power ") + String(level / 4.0f, 1) + "dBm: " + (ok ? "ok" : "FAILED"));
#endif
}

float NetworkManager::getTxPowerDbm() {
#if defined(PLATFORM_ESP32)
    return WiFi.getTxPower() / 4.0f;
#else
    return 0;
#endif
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
    if (g_disconnectPending) {
        g_disconnectPending = false;
        lastDisconnectReason_ = g_disconnectReason;
        Logger::warn(TAG, "Wi-Fi disconnect reason " + String(lastDisconnectReason_) + ": " +
                              disconnectReasonText(lastDisconnectReason_));
    }

    if (mode_ == NetworkMode::PROVISIONING_AP) {
        dnsServer_.processNextRequest();
        retryConfiguredNetworkFromAP();
        return;
    }

#if defined(PLATFORM_ESP32)
    // ESPmDNS needs no periodic pump.
#elif defined(PLATFORM_ESP8266)
    MDNS.update();
#endif

    if (WiFi.status() == WL_CONNECTED) {
        if (disconnectedSinceMs_ != 0) {
            Logger::info(TAG, "Wi-Fi back after " + String((millis() - disconnectedSinceMs_) / 1000) +
                                  "s and " + String(reconnectFailures_) + " attempt(s): IP " +
                                  WiFi.localIP().toString() + ", ch " + String(WiFi.channel()) +
                                  ", " + String(WiFi.RSSI()) + "dBm");
            disconnectedSinceMs_ = 0;
        }
        reconnectFailures_ = 0;
        if (!everConnected_) {
            everConnected_ = true;
            setupMDNS();
        }
        return;
    }

    // ---- Link is down ----
    unsigned long now = millis();
    if (disconnectedSinceMs_ == 0) {
        disconnectedSinceMs_ = now ? now : 1;
        reconnectFailures_ = 0;
        lastConnectAttemptMs_ = 0; // act on the first loop pass, don't wait out an interval
        Logger::warn(TAG, "Wi-Fi link is down");
    }

    if (now - disconnectedSinceMs_ >= STUCK_RESTART_MS) {
        Logger::error(TAG, "Offline for " + String(STUCK_RESTART_MS / 60000) + " min despite " +
                               String(reconnectFailures_) + " attempts — restarting");
        delay(200); // let the line reach the serial port
        PlatformManager::restart();
    }

    if (lastConnectAttemptMs_ != 0 && now - lastConnectAttemptMs_ < RECONNECT_INTERVAL_MS) return;
    lastConnectAttemptMs_ = now;
    reconnectCount_++;

    if (reconnectFailures_++ == 0) {
        // First response to a drop: the cheap path. reconnect() re-uses the
        // AP and channel from the last successful connect, which is right
        // for a momentary blip.
        Logger::warn(TAG, "Attempting reconnect to the same AP");
        WiFi.reconnect();
    } else {
        // That didn't hold. Every later attempt is a full one: stop the
        // pending connect, scan, and target the strongest AP carrying this
        // SSID — the AP may have moved channel, or a different AP in the
        // same network may be the reachable one now. maxWaitMs = 0 so the
        // attempt is started and left to run; the RECONNECT_INTERVAL_MS gap
        // before the next one is what gives it time to complete.
        Logger::warn(TAG, "Still offline (" + String((now - disconnectedSinceMs_) / 1000) +
                              "s) — re-scanning, attempt " + String(reconnectFailures_));
        performConnectAttempt(config_.get(), 0);
    }
}

// While parked in the provisioning AP, periodically try the configured
// network again. Without this the AP was a one-way trip: any outage that
// outlasted begin()'s ~60s of connect attempts — or a watchdog reset during
// an outage — left the device serving its setup AP forever, even after the
// network came back. That is the "device does not come back online" failure.
void NetworkManager::retryConfiguredNetworkFromAP() {
    if (!config_.isProvisioned()) return; // no stored network to go back to

    // Don't yank the AP out from under someone who is mid-provisioning.
    if (WiFi.softAPgetStationNum() > 0) {
        lastApRetryMs_ = millis(); // and don't pounce the moment they leave
        return;
    }

    unsigned long now = millis();
    if (lastApRetryMs_ != 0 && now - lastApRetryMs_ < AP_RETRY_INTERVAL_MS) return;
    lastApRetryMs_ = now;

    const DeviceConfig &c = config_.get();
    Logger::info(TAG, "Retrying \"" + c.wifiSsid + "\" from provisioning AP");

    dnsServer_.stop();
    WiFi.mode(WIFI_STA);
    applyStationPolicy();
    applyTxPower();

    // ONE bounded attempt, not connectSTA()'s full ~60s budget: this runs
    // inside loop() with the watchdog armed, unlike the boot-time connect.
    if (performConnectAttempt(c, AP_RETRY_WAIT_MS)) {
        mode_ = NetworkMode::STATION;
        everConnected_ = true;
        reconnectFailures_ = 0;
        disconnectedSinceMs_ = 0;
        lastConnectAttemptMs_ = millis();
        Logger::info(TAG, "Rejoined " + c.wifiSsid + " (" + WiFi.localIP().toString() +
                              "), leaving provisioning AP");
        setupMDNS();
    } else {
        Logger::warn(TAG, "Still cannot reach \"" + c.wifiSsid + "\", staying in provisioning AP");
        startProvisioningAP(); // re-arms AP mode and the captive-portal DNS
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

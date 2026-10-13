#pragma once
// -----------------------------------------------------------------------------
// NetworkManager.h
//
// Wi-Fi station lifecycle: connect, non-blocking reconnect with backoff,
// first-boot / on-demand provisioning AP with a captive-portal DNS redirect,
// and mDNS (sections 11-13, 33).
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <DNSServer.h>
#include "config/ConfigManager.h"

#if defined(PLATFORM_ESP32)
#include <WiFi.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266WiFi.h>
#endif

enum class NetworkMode { STATION, PROVISIONING_AP };

class NetworkManager {
public:
    explicit NetworkManager(ConfigManager &config);

    void begin();
    void loop();

    bool isConnected();
    NetworkMode mode() const { return mode_; }
    bool isProvisioning() const { return mode_ == NetworkMode::PROVISIONING_AP; }

    String getSSID();
    String getIPAddress();
    String getGatewayIP();
    String getApSSID() const { return apSsid_; }
    int getRSSI();
    int32_t getChannel();
    uint32_t getReconnectCount() const { return reconnectCount_; }
    // 802.11 / vendor reason code for the most recent link loss, and a short
    // plain-language reading of it. 0 / "none" until a drop has happened.
    uint8_t getLastDisconnectReason() const { return lastDisconnectReason_; }
    static const char *disconnectReasonText(uint8_t reason);

    // Enters provisioning AP mode immediately (factory reset / button /
    // repeated connect failure). Idempotent.
    void startProvisioningAP();

    // Called after Wi-Fi credentials are saved via the settings/provisioning
    // page — attempts to leave AP mode and join the new network.
    void applyNewCredentials();

    // Applies the configured Wi-Fi transmit power to the running radio.
    void applyTxPower();
    // The radio's actual transmit power in dBm (0 where not readable).
    float getTxPowerDbm();

private:
    ConfigManager &config_;
    NetworkMode mode_ = NetworkMode::PROVISIONING_AP;
    DNSServer dnsServer_;
    String apSsid_;

    uint32_t reconnectCount_ = 0;
    uint8_t connectAttempts_ = 0;
    unsigned long lastConnectAttemptMs_ = 0;
    unsigned long lastRssiCheckMs_ = 0;
    bool everConnected_ = false;

    // --- Outage handling (see loop()) ---------------------------------------
    // Attempts made during the current outage; 0 while connected.
    uint8_t reconnectFailures_ = 0;
    // millis() when the link was first seen down; 0 while connected.
    unsigned long disconnectedSinceMs_ = 0;
    // Time between attempts while offline. Was 5s, which is shorter than a
    // join + DHCP can take on a weak or busy link — and every attempt starts
    // by tearing down the one in progress, so a short interval can cancel
    // its own attempts indefinitely.
    static constexpr unsigned long RECONNECT_INTERVAL_MS = 15000;
    // Last resort. Observed live: a device that had failed every reconnect
    // for over a minute joined the same AP immediately after a power cycle.
    // If nothing has worked for this long, restart rather than stay dark.
    // Safe against boot loops: a restart during a real outage ends up in the
    // provisioning AP (which keeps retrying), not back in this branch.
    static constexpr unsigned long STUCK_RESTART_MS = 10UL * 60UL * 1000UL;

    // Consecutive scans that completed but did not list the configured SSID.
    // That is what a hidden network looks like: it can be joined by name but
    // never ranked, so the ~6s scan buys nothing. After two such scans the
    // scan is skipped, and re-tried only on every 5th attempt in case the
    // network has since become visible.
    uint8_t unseenScans_ = 0;
    uint8_t scanSkipCounter_ = 0;

    // Most recent disconnect reason reported by the Wi-Fi stack (0 = none
    // yet). Kept so a drop can be explained from /api/network afterwards
    // instead of only from a serial log nobody was watching.
    uint8_t lastDisconnectReason_ = 0;

    // Provisioning-AP escape hatch: while parked in the AP after a failed
    // connect, periodically try the configured network again, so an outage
    // that outlasts boot isn't a permanent one-way trip into setup mode.
    unsigned long lastApRetryMs_ = 0;
    static constexpr unsigned long AP_RETRY_INTERVAL_MS = 60000;
    // Kept under main.cpp's 15s watchdog: scan (~2.5s) + this wait.
    static constexpr uint32_t AP_RETRY_WAIT_MS = 8000;
    void retryConfiguredNetworkFromAP();

    void connectSTA(bool blockingFirstAttempt);
    // One full connect attempt: scan (to pick the strongest matching BSSID
    // and detect WEP), WiFi.begin(), then block up to maxWaitMs waiting for
    // WL_CONNECTED. Returns true on success. See connectSTA()'s retry loop.
    bool performConnectAttempt(const DeviceConfig &c, uint32_t maxWaitMs);
    void setupMDNS();
    String deriveApSsid();
    String deriveMdnsHostname();
};

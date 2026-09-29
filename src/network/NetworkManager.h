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

    // Enters provisioning AP mode immediately (factory reset / button /
    // repeated connect failure). Idempotent.
    void startProvisioningAP();

    // Called after Wi-Fi credentials are saved via the settings/provisioning
    // page — attempts to leave AP mode and join the new network.
    void applyNewCredentials();

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

    // Consecutive failed WiFi.reconnect() calls. reconnect() reuses the
    // BSSID/channel latched at the last successful connect, so if the AP
    // came back on a different channel — or a different AP in the same ESS
    // is the reachable one now — it can never succeed. After this many
    // failures, fall back to a full re-scan instead of retrying blind.
    uint8_t reconnectFailures_ = 0;
    static constexpr uint8_t RESCAN_AFTER_FAILURES = 6; // ~30s of 5s retries

    // Provisioning-AP escape hatch: while parked in the AP after a failed
    // connect, periodically try the configured network again, so an outage
    // that outlasts boot isn't a permanent one-way trip into setup mode.
    unsigned long lastApRetryMs_ = 0;
    static constexpr unsigned long AP_RETRY_INTERVAL_MS = 60000;
    // Kept well under main.cpp's 15s watchdog: scan (~2s) + this wait.
    static constexpr uint32_t AP_RETRY_WAIT_MS = 5000;
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

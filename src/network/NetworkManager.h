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

    void connectSTA(bool blockingFirstAttempt);
    void setupMDNS();
    String deriveApSsid();
    String deriveMdnsHostname();
};

#include "NetworkProbe.h"
#include "hardware/PingCompat.h"
#include "util/Logger.h"

#if defined(PLATFORM_ESP32)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
using SecureClient = WiFiClientSecure;
#elif defined(PLATFORM_ESP8266)
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
using SecureClient = BearSSL::WiFiClientSecure;
#endif

static const char *TAG = "NetworkProbe";

NetworkProbe::NetworkProbe(ConfigManager &config, NetworkManager &network)
    : config_(config), network_(network) {
    results_[(uint8_t)ProbeId::GATEWAY].label = "Gateway";
    results_[(uint8_t)ProbeId::PING_1].label = "Probe Target 1";
    results_[(uint8_t)ProbeId::PING_2].label = "Probe Target 2";
    results_[(uint8_t)ProbeId::DNS].label = "DNS";
    results_[(uint8_t)ProbeId::HTTP].label = "HTTP/HTTPS";
}

void NetworkProbe::loop() {
    if (!network_.isConnected()) return; // nothing meaningful to probe in AP/provisioning mode

    uint32_t intervalMs = config_.get().networkIntervalS * 1000UL;
    unsigned long now = millis();

    for (uint8_t i = 0; i < (uint8_t)ProbeId::COUNT; i++) {
        uint8_t idx = (cursor_ + i) % (uint8_t)ProbeId::COUNT;
        bool due = (lastRunMs_[idx] == 0) || (now - lastRunMs_[idx] >= intervalMs);
        if (due) {
            lastRunMs_[idx] = now;
            cursor_ = (idx + 1) % (uint8_t)ProbeId::COUNT;
            runProbe((ProbeId)idx);
            return; // at most one probe per loop() call
        }
    }
}

void NetworkProbe::runProbe(ProbeId id) {
    NetworkProbeResult &r = results_[(uint8_t)id];
    switch (id) {
        case ProbeId::GATEWAY: probeGateway(r); break;
        case ProbeId::PING_1:  probePing(r, config_.get().pingTarget1); break;
        case ProbeId::PING_2:  probePing(r, config_.get().pingTarget2); break;
        case ProbeId::DNS:     probeDns(r); break;
        case ProbeId::HTTP:    probeHttp(r); break;
        default: break;
    }
    r.everRun = true;
    r.timestamp = millis() / 1000;
}

void NetworkProbe::probeGateway(NetworkProbeResult &r) {
    String target = config_.get().gatewayTarget;
    if (target.length() == 0) target = network_.getGatewayIP();
    probePing(r, target);
}

void NetworkProbe::probePing(NetworkProbeResult &r, const String &target) {
    r.target = target;
    IPAddress ip;
    if (target.length() == 0 || !ip.fromString(target)) {
        r.reachable = false;
        r.latencyMs = 0;
        r.packetLossPercent = 100;
        r.extra = "invalid target";
        return;
    }

    float avgMs = 0, lossPct = 0;
    bool ok = pingcompat::ping(ip, config_.get().probePacketCount, avgMs, lossPct);
    r.reachable = ok;
    r.latencyMs = avgMs;
    r.packetLossPercent = lossPct;
    r.extra = "";

    if (lossPct > 0 && lossPct < 100) {
        Logger::warn(TAG, target + " packet loss " + String(lossPct, 0) + "%");
    } else if (!ok) {
        Logger::warn(TAG, target + " unreachable");
    }
}

void NetworkProbe::probeDns(NetworkProbeResult &r) {
    String domain = config_.get().dnsDomain;
    r.target = domain;

    IPAddress resolved;
    unsigned long start = millis();
    bool ok = WiFi.hostByName(domain.c_str(), resolved) == 1;
    unsigned long elapsed = millis() - start;

    r.reachable = ok;
    r.latencyMs = (float)elapsed;
    r.packetLossPercent = ok ? 0 : 100;
    r.extra = ok ? resolved.toString() : "resolution failed";

    if (!ok) Logger::warn(TAG, "DNS resolution failed for " + domain);
}

void NetworkProbe::probeHttp(NetworkProbeResult &r) {
    String url = config_.get().httpTarget;
    r.target = url;

    bool https = url.startsWith("https://");
    uint32_t timeout = config_.get().probeTimeoutMs;

    HTTPClient http;
    http.setTimeout(timeout);
    unsigned long start = millis();

    int httpCode = -1;
    if (https) {
        SecureClient client;
        client.setInsecure(); // Phase 1: reachability/latency check only, not a
                               // certificate-trust decision — see docs/architecture.md.
        if (http.begin(client, url)) {
            httpCode = http.GET();
        }
    } else {
        WiFiClient client;
        if (http.begin(client, url)) {
            httpCode = http.GET();
        }
    }
    unsigned long elapsed = millis() - start;
    http.end();

    r.latencyMs = (float)elapsed;
    r.reachable = httpCode > 0;
    r.packetLossPercent = r.reachable ? 0 : 100;
    r.extra = r.reachable ? String("HTTP ") + httpCode : String("no response");

    if (!r.reachable) Logger::warn(TAG, "HTTP probe failed for " + url);
}

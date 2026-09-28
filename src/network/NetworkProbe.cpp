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

#if defined(PLATFORM_ESP8266)
// Bare host (scheme/path/port stripped) for probeMaxFragmentLength(), which
// takes a hostname, not a URL. Same idiom as Gen2Telemetry.cpp.
static String extractHost(const String &url) {
    String host = url;
    int schemeEnd = host.indexOf("://");
    if (schemeEnd >= 0) host = host.substring(schemeEnd + 3);
    int pathStart = host.indexOf('/');
    if (pathStart >= 0) host = host.substring(0, pathStart);
    int portStart = host.indexOf(':');
    if (portStart >= 0) host = host.substring(0, portStart);
    return host;
}
#endif

NetworkProbe::NetworkProbe(ConfigManager &config, NetworkManager &network)
    : config_(config), network_(network) {}

void NetworkProbe::syncWithConfig() {
    const DeviceConfig &c = config_.get();

    NetworkProbeResult rebuilt[hw::MAX_MONITORS];
    unsigned long rebuiltLast[hw::MAX_MONITORS] = {0};

    for (uint8_t i = 0; i < c.monitorCount; i++) {
        const MonitorDef &m = c.monitors[i];
        // Carry over this monitor's existing result, matched by id, so
        // editing or deleting one monitor doesn't blank everything else's
        // last-known state.
        for (size_t j = 0; j < count_; j++) {
            if (results_[j].monitorId.length() > 0 && results_[j].monitorId == m.id) {
                rebuilt[i] = results_[j];
                rebuiltLast[i] = lastRunMs_[j];
                break;
            }
        }
        rebuilt[i].monitorId = m.id;
        rebuilt[i].label = m.name; // a rename should show immediately
    }

    for (uint8_t i = 0; i < c.monitorCount; i++) {
        results_[i] = rebuilt[i];
        lastRunMs_[i] = rebuiltLast[i];
    }
    for (uint8_t i = c.monitorCount; i < hw::MAX_MONITORS; i++) {
        results_[i] = NetworkProbeResult();
        lastRunMs_[i] = 0;
    }

    count_ = c.monitorCount;
    if (count_ == 0 || cursor_ >= count_) cursor_ = 0;
    generation_ = config_.monitorsGeneration();
}

void NetworkProbe::loop() {
    // Cheap integer compare every tick; the rebuild only runs when the user
    // (or GEN2 sync) actually changed the list.
    if (config_.monitorsGeneration() != generation_) syncWithConfig();

    if (!network_.isConnected()) return; // nothing meaningful to probe in AP/provisioning mode
    if (count_ == 0) return;             // no monitors configured

    const DeviceConfig &c = config_.get();
    uint32_t intervalMs = c.networkIntervalS * 1000UL;
    unsigned long now = millis();

    for (size_t i = 0; i < count_; i++) {
        size_t idx = (cursor_ + i) % count_;
        bool due = (lastRunMs_[idx] == 0) || (now - lastRunMs_[idx] >= intervalMs);
        if (due) {
            lastRunMs_[idx] = now;
            cursor_ = (idx + 1) % count_;
            runProbe(idx, c.monitors[idx]);
            return; // at most one probe per loop() call
        }
    }
}

void NetworkProbe::runProbe(size_t idx, const MonitorDef &m) {
    NetworkProbeResult &r = results_[idx];
    unsigned long wallStart = millis(); // wraps whichever probe fn's own
                                         // internal timing, for a
                                         // diagnostic view of how long each
                                         // probe type actually blocks loop()

    switch (m.type) {
        case MonitorType::PING: {
            // A blank ping target still means "the DHCP-learned gateway" —
            // that is how the old fixed Gateway probe behaved, and the
            // migrated Gateway monitor relies on it.
            String target = m.target;
            if (target.length() == 0) target = network_.getGatewayIP();
            probePing(r, target);
            break;
        }
        case MonitorType::DNS:  probeDns(r, m.target); break;
        case MonitorType::HTTP: probeHttp(r, m.target); break;
        case MonitorType::PORT: probePort(r, m.target, m.port); break;
    }

    r.everRun = true;
    r.timestamp = millis() / 1000;

    unsigned long wallElapsed = millis() - wallStart;
    if (wallElapsed > 1000) {
        // Any single probe blocking loop() for over a second is worth
        // knowing about — this is what starves WiFi/webserver servicing.
        Logger::info(TAG, r.label + " probe blocked loop() for " + String(wallElapsed) + "ms");
    }
}

void NetworkProbe::probePing(NetworkProbeResult &r, const String &target) {
    r.target = target;

    if (target.length() == 0) {
        r.reachable = false;
        r.latencyMs = 0;
        r.packetLossPercent = 100;
        r.extra = "no target";
        return;
    }

    IPAddress ip;
    if (!ip.fromString(target)) {
        // The old fixed probes only ever pinged literal IPs, but a monitor
        // GEN2 dispatches can name a host, so resolve before giving up.
        if (WiFi.hostByName(target.c_str(), ip) != 1) {
            r.reachable = false;
            r.latencyMs = 0;
            r.packetLossPercent = 100;
            r.extra = "cannot resolve";
            return;
        }
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

void NetworkProbe::probeDns(NetworkProbeResult &r, const String &domain) {
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

void NetworkProbe::probeHttp(NetworkProbeResult &r, const String &url) {
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
#if defined(PLATFORM_ESP8266)
        // Same fix as Gen2Telemetry.cpp: BearSSL::WiFiClientSecure defaults
        // to a 16KB+512B buffer, too large a contiguous allocation for this
        // device's small free heap to reliably (or quickly) satisfy against
        // an arbitrary user-configured HTTPS target — confirmed live to
        // cause multi-second stalls (and consequent WiFi packet loss
        // elsewhere) on this exact probe, recurring every networkIntervalS.
        // Negotiate a smaller buffer via MFLN when the target supports it.
        if (SecureClient::probeMaxFragmentLength(extractHost(url), 443, 1024)) {
            client.setBufferSizes(1024, 512);
        }
#endif
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

void NetworkProbe::probePort(NetworkProbeResult &r, const String &host, uint16_t port) {
    r.target = host + ":" + String(port);

    uint32_t timeout = config_.get().probeTimeoutMs;
    WiFiClient client;

    unsigned long start = millis();
#if defined(PLATFORM_ESP32)
    // ESP32's WiFiClient takes the connect timeout as an explicit argument;
    // its setTimeout() is in seconds, which would silently round 1500ms to 1s.
    bool ok = client.connect(host.c_str(), port, (int32_t)timeout);
#else
    // ESP8266's setTimeout() is milliseconds and applies to connect().
    client.setTimeout(timeout);
    bool ok = client.connect(host.c_str(), port);
#endif
    unsigned long elapsed = millis() - start;
    client.stop();

    r.reachable = ok;
    r.latencyMs = (float)elapsed;
    r.packetLossPercent = ok ? 0 : 100;
    // "open" means the TCP handshake completed — it says nothing about what
    // is listening there, same spirit as the HTTP probe's any-response rule.
    r.extra = ok ? String("open") : String("closed/unreachable");

    if (!ok) Logger::warn(TAG, "Port probe failed for " + r.target);
}

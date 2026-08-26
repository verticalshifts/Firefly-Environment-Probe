#include "Gen2Telemetry.h"
#include "hardware/Gen2RootCA.h"
#include "util/Logger.h"
#include <ArduinoJson.h>

#if defined(PLATFORM_ESP32)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
using SecureClient = WiFiClientSecure;
#elif defined(PLATFORM_ESP8266)
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
using SecureClient = BearSSL::WiFiClientSecure;
#endif

static const char *TAG = "Gen2";
static const char *GEN2_URL = "https://g2i.batbapps.com/groundprobe";

Gen2Telemetry::Gen2Telemetry(ConfigManager &config, NetworkManager &network, DeviceManager &device)
    : config_(config), network_(network), device_(device) {}

bool Gen2Telemetry::publishEnvironment(const EnvironmentReading &reading, const char *sensorType) {
    (void)sensorType; // GEN2's groundprobe endpoint has no sensor-type field

    const DeviceConfig &c = config_.get();
    if (!c.gen2Enabled) return true; // opt-in, disabled by default — silent no-op

    // Non-blocking millis() gate, mirroring EnvironmentManager::loop() and
    // NetworkProbe::loop()'s idiom. main.cpp may call this once per
    // environment read cycle (~every 10s by default); the actual HTTPS
    // POST only fires at most once per gen2IntervalS (default 60s).
    unsigned long now = millis();
    uint32_t intervalMs = c.gen2IntervalS * 1000UL;
    if (lastPostMs_ != 0 && (now - lastPostMs_ < intervalMs)) return true; // not due yet

    if (!network_.isConnected()) return true; // nothing to POST to off Wi-Fi

    lastPostMs_ = now; // mark attempted regardless of outcome, so a slow/failed
                        // POST doesn't cause a tight retry loop next call

    return postEnvironment(reading);
}

bool Gen2Telemetry::postEnvironment(const EnvironmentReading &reading) {
    const DeviceConfig &c = config_.get();

    String monitor = c.gen2MonitorName.length() > 0 ? c.gen2MonitorName : c.deviceName;

    // "UP"/"DOWN" is compared exactly by GEN2's alerting transition logic
    // server-side — meaningful reuse, not a hack: GEN2 will correctly alert
    // if the DHT sensor starts failing, not just if the network drops.
    const char *status = reading.valid ? "UP" : "DOWN";

    JsonDocument doc;
    doc["license_key"] = c.gen2LicenseKey;
    doc["org_id"] = c.gen2OrgId;
    doc["monitor"] = monitor;
    doc["status"] = status;
    doc["server_id"] = device_.getDeviceId();
    doc["temperature"] = reading.temperature; // extra field GEN2 currently drops
    doc["humidity"] = reading.humidity;       // server-side — intentional, see .h
    // latency_ms is the *previous* GEN2 POST's measured round-trip time, not
    // this request's own — a request's body can't contain its own not-yet-
    // measured RTT. One interval stale (0 on the very first publish) is
    // still honest telemetry: "GEN2 reachability latency from this device."
    doc["latency_ms"] = lastLatencyMs_;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setTimeout(c.probeTimeoutMs);

    SecureClient client;
#if defined(PLATFORM_ESP32)
    client.setCACert(GEN2_ROOT_CA);
#elif defined(PLATFORM_ESP8266)
    static BearSSL::X509List rootCert(GEN2_ROOT_CA); // parsed once, kept alive
                                                       // for setTrustAnchors' pointer
    client.setTrustAnchors(&rootCert);
#endif

    if (!http.begin(client, GEN2_URL)) {
        Logger::warn(TAG, "Failed to begin HTTPS connection to GEN2");
        return false;
    }
    http.addHeader("Content-Type", "application/json");

    unsigned long start = millis();
    int httpCode = http.POST(body);
    unsigned long elapsed = millis() - start;
    http.end();

    lastLatencyMs_ = (float)elapsed;

    if (httpCode == 401) {
        Logger::warn(TAG, "GEN2 rejected license_key (401) — check gen2LicenseKey/gen2OrgId in Settings");
    } else if (httpCode <= 0) {
        Logger::warn(TAG, "GEN2 POST failed (no response), rtt=" + String(elapsed) + "ms");
    } else if (httpCode != 200) {
        Logger::warn(TAG, "GEN2 POST returned HTTP " + String(httpCode));
    } else {
        Logger::info(TAG, "GEN2 publish OK, rtt=" + String(elapsed) + "ms");
    }

    return httpCode == 200;
}

bool Gen2Telemetry::publishNetwork(const NetworkProbeResult results[], size_t count) {
    (void)results;
    (void)count;
    // Out of scope for now — see Gen2Telemetry.h.
    return true;
}

bool Gen2Telemetry::publishDeviceStatus(const DeviceStatus &status) {
    (void)status;
    return true;
}

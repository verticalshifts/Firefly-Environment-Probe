#include "IotGatewayTelemetry.h"
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

static const char *TAG = "IoTGW";

IotGatewayTelemetry::IotGatewayTelemetry(ConfigManager &config, NetworkManager &network)
    : config_(config), network_(network) {}

bool IotGatewayTelemetry::publishEnvironment(const EnvironmentReading &reading, const char *sensorType) {
    (void)sensorType; // the gateway's ingest contract has no sensor-type field

    const DeviceConfig &c = config_.get();
    if (!c.iotgwEnabled) return true; // opt-in, disabled by default — silent no-op

    // Same non-blocking gate as Gen2Telemetry: called every loop tick, POSTs
    // at most once per iotgwIntervalS, and marks the attempt before sending so
    // a failing gateway can't cause a tight retry loop.
    unsigned long now = millis();
    if (lastPostMs_ != 0 && (now - lastPostMs_ < c.iotgwIntervalS * 1000UL)) return true;
    if (!network_.isConnected()) return true;
    lastPostMs_ = now;

    return postEnvironment(reading);
}

bool IotGatewayTelemetry::postEnvironment(const EnvironmentReading &reading) {
    const DeviceConfig &c = config_.get();

    JsonDocument doc;
    doc["temperature"] = reading.temperature;
    doc["humidity"] = reading.humidity;
    // No "water_leak" — see the header comment.

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setTimeout(c.probeTimeoutMs);

    WiFiClient plainClient;
    SecureClient secureClient;
    bool https = c.iotgwUrl.startsWith("https://");
    bool begun;
    if (https) {
        secureClient.setInsecure(); // see header: same trade-off as Gen2Telemetry
        begun = http.begin(secureClient, c.iotgwUrl);
    } else {
        begun = http.begin(plainClient, c.iotgwUrl);
    }
    if (!begun) {
        Logger::warn(TAG, "Failed to begin connection to " + c.iotgwUrl);
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + c.iotgwToken);

    unsigned long start = millis();
    int httpCode = http.POST(body);
    unsigned long elapsedMs = millis() - start;

    bool ok = httpCode >= 200 && httpCode < 300;
    if (httpCode == 401 || httpCode == 403) {
        Logger::warn(TAG, "Gateway rejected the token (HTTP " + String(httpCode) + ") — check iotgwToken in Settings");
    } else if (httpCode <= 0) {
        Logger::warn(TAG, "Gateway POST failed (no response): " + http.errorToString(httpCode) +
                               ", " + String(elapsedMs) + "ms");
    } else if (!ok) {
        // Bodies are short JSON error messages; include a bounded snippet so a
        // validation rejection is diagnosable from the serial log alone.
        String resp = http.getString();
        if (resp.length() > 160) resp = resp.substring(0, 160);
        Logger::warn(TAG, "Gateway POST returned HTTP " + String(httpCode) + ": " + resp);
    } else {
        Logger::info(TAG, "Gateway publish OK (HTTP " + String(httpCode) + ", " + String(elapsedMs) + "ms)");
    }
    http.end();
    return ok;
}

bool IotGatewayTelemetry::publishNetwork(const NetworkProbeResult results[], size_t count) {
    (void)results;
    (void)count;
    return true;
}

bool IotGatewayTelemetry::publishDeviceStatus(const DeviceStatus &status) {
    (void)status;
    return true;
}

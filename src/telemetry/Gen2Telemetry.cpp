#include "Gen2Telemetry.h"
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
// Fixed path suffix appended to the user-configured gen2ServerUrl. Live-
// verified this is the one path that resolves correctly on every GEN2 host
// seen so far (a bare "/groundprobe" only works on g2i.batbapps.com — on
// gen2bullseye.com it falls through to the SPA's HTML instead of the API).
static const char *GEN2_PATH = "/api/groundprobe";

// Strips exactly one trailing slash so "https://host/" + "/api/..." doesn't
// produce a double slash if the user's saved URL has one.
static String buildGen2Url(const String &serverUrl) {
    String base = serverUrl;
    if (base.endsWith("/")) base.remove(base.length() - 1);
    return base + GEN2_PATH;
}

// Bare host (no scheme/path/port) — used both by ESP8266's
// probeMaxFragmentLength() and by measureNetworkLatency() below, neither of
// which take a full URL.
static String extractHost(const String &serverUrl) {
    String host = serverUrl;
    int schemeEnd = host.indexOf("://");
    if (schemeEnd >= 0) host = host.substring(schemeEnd + 3);
    int pathStart = host.indexOf('/');
    if (pathStart >= 0) host = host.substring(0, pathStart);
    int portStart = host.indexOf(':');
    if (portStart >= 0) host = host.substring(0, portStart);
    return host;
}

// Reports how long a bare TCP connect to host:port takes — no TLS involved.
// This is what gets sent to GEN2 as latency_ms, deliberately *not* the full
// secure POST's duration: that duration is dominated by on-device
// certificate-verification CPU time (RSA-4096 chains take seconds on an
// 80MHz ESP8266 — see Gen2RootCA.h), which is a device/CPU cost, not network
// latency, and was showing up as false "high latency" alerts on GEN2 as a
// result. A plain connect() still crosses the same network path to the same
// host, so it's an honest stand-in for reachability latency without that
// contamination. Bounded by timeoutMs so a dead host can't stall the caller
// past that; connection (if it succeeded) is closed immediately after.
static float measureNetworkLatency(const String &host, uint16_t port, uint32_t timeoutMs) {
    WiFiClient probe;
    probe.setTimeout(timeoutMs);
    unsigned long start = millis();
    probe.connect(host.c_str(), port); // return value not used — elapsed time
                                        // is meaningful whether it succeeded
                                        // or timed out (the latter honestly
                                        // means "very high/no latency" too)
    unsigned long elapsed = millis() - start;
    probe.stop();
    return (float)elapsed;
}

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

    String host = extractHost(c.gen2ServerUrl);

    // Measured fresh, before the POST — a plain TCP connect (see
    // measureNetworkLatency's comment for why not the secure POST's own
    // duration). Fixes the original "can't embed a request's own not-yet-
    // measured RTT" circularity properly (this is a separate, already-
    // completed measurement, not the POST's own timing) rather than the
    // previous workaround of sending one interval's stale value.
    float latencyMs = measureNetworkLatency(host, 443, c.probeTimeoutMs);

    JsonDocument doc;
    doc["license_key"] = c.gen2LicenseKey;
    doc["org_id"] = c.gen2OrgId;
    doc["monitor"] = monitor;
    doc["status"] = status;
    doc["server_id"] = device_.getDeviceId();
    doc["temperature"] = reading.temperature; // extra field GEN2 currently drops
    doc["humidity"] = reading.humidity;       // server-side — intentional, see .h
    doc["latency_ms"] = latencyMs;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setTimeout(c.probeTimeoutMs);

    SecureClient client;
    // setInsecure(): deliberately not certificate-verified, despite this POST
    // carrying a secret license key — see Gen2Telemetry.h for the tradeoff.
    // Confirmed live: verifying the pinned root chain (RSA-4096, ISRG Root
    // X1) took ~12.4s of CPU-bound signature verification on this chip,
    // blocking loop() long enough to cause real WiFi packet loss (ICMP
    // ping timeouts measured during that exact window). setInsecure() skips
    // chain validation entirely, cutting the handshake to a small fraction
    // of that.
    client.setInsecure();

#if defined(PLATFORM_ESP8266)
    // ESP8266 has very little free heap (as little as ~25KB observed on this
    // device with Wi-Fi + web server + JSON already running), and
    // BearSSL::WiFiClientSecure defaults to a 16KB+512B TLS record buffer —
    // too large a contiguous allocation to reliably succeed on that budget,
    // which makes connect() fail near-instantly (confirmed live: this is
    // also why NetworkProbe's existing HTTP/HTTPS ground probe reports
    // "unreachable" in ~30ms against arbitrary HTTPS targets on this same
    // board). This is a memory constraint independent of setInsecure()
    // above, so it still applies. Negotiating Maximum Fragment Length down
    // to 1024 bytes first — supported by most modern TLS front ends — lets
    // a far smaller buffer work, actually fitting this device's heap.
    bool mfln = SecureClient::probeMaxFragmentLength(host, 443, 1024);
    if (mfln) {
        client.setBufferSizes(1024, 512);
    } // else: leave the (likely doomed on this heap) default and let the
      // existing httpCode <= 0 handling report the failure as today.
    Logger::info(TAG, "MFLN=" + String(mfln ? "yes" : "no") + ", freeHeap=" + String(ESP.getFreeHeap()));
#endif

    String url = buildGen2Url(c.gen2ServerUrl);
    if (!http.begin(client, url)) {
        Logger::warn(TAG, "Failed to begin HTTPS connection to GEN2");
        return false;
    }
    http.addHeader("Content-Type", "application/json");

    // This is the full secure POST's own duration — logged for our own
    // diagnostics only. Deliberately *not* what's sent to GEN2 as
    // latency_ms (see measureNetworkLatency above): it includes on-device
    // TLS handshake + certificate-verification time, which can be seconds
    // on this CPU and would misrepresent real network latency.
    unsigned long start = millis();
    int httpCode = http.POST(body);
    unsigned long publishMs = millis() - start;
    http.end();

    if (httpCode == 401) {
        Logger::warn(TAG, "GEN2 rejected license_key (401) — check gen2LicenseKey/gen2OrgId in Settings");
    } else if (httpCode <= 0) {
        String detail;
#if defined(PLATFORM_ESP8266)
        char sslErr[128] = {0};
        client.getLastSSLError(sslErr, sizeof(sslErr));
        detail = " sslErr=" + String(sslErr);
#endif
        Logger::warn(TAG, "GEN2 POST failed (no response), publish=" + String(publishMs) + "ms" +
                               " httpCode=" + String(httpCode) + detail);
    } else if (httpCode != 200) {
        Logger::warn(TAG, "GEN2 POST returned HTTP " + String(httpCode));
    } else {
        Logger::info(TAG, "GEN2 publish OK, publish=" + String(publishMs) + "ms, latency_ms sent=" + String(latencyMs));
    }

    return httpCode == 200;
}

bool Gen2Telemetry::publishNetwork(const NetworkProbeResult results[], size_t count) {
    const DeviceConfig &c = config_.get();
    // Syncing monitors down from GEN2 implies reporting them back up. GEN2
    // marks any probe DOWN once nothing has reported on it for
    // expected_interval + grace (~90s at its 60s default, see the staleness
    // checker in storage.ts), so a dispatched monitor the device never
    // publishes is guaranteed to alarm as DOWN. "Sync on, publish off" is an
    // invalid state that only produces false alerts, so sync enables
    // publishing rather than letting the two disagree.
    if (!c.gen2PublishMonitors && !c.gen2SyncEnabled) return true;
    if (count == 0) return true;
    if (!network_.isConnected()) return true;

    // One monitor per tick, evenly spaced across gen2IntervalS, so each
    // monitor reports once per interval without ever firing `count`
    // blocking TLS POSTs back to back — and so a device with a full monitor
    // list stays well under GEN2's probeLimiter (120 requests/min, keyed by
    // IP and therefore shared by every device behind the same NAT).
    unsigned long now = millis();
    uint32_t spacingMs = (c.gen2IntervalS * 1000UL) / count;
    if (spacingMs < 1000) spacingMs = 1000; // never hammer, however many monitors
    if (lastMonitorPostMs_ != 0 && (now - lastMonitorPostMs_ < spacingMs)) return true;
    lastMonitorPostMs_ = now;

    if (monitorCursor_ >= count) monitorCursor_ = 0;
    const NetworkProbeResult &r = results[monitorCursor_];
    monitorCursor_ = (monitorCursor_ + 1) % count;

    // Nothing meaningful to say about a monitor that hasn't run yet; skip it
    // rather than inventing a DOWN that GEN2 would alert on. Logged because a
    // monitor stuck in this state never reaches GEN2 at all, which otherwise
    // looks identical to a publishing failure.
    if (!r.everRun) {
        Logger::info(TAG, "Monitor \"" + r.label + "\" not published yet (no probe result)");
        return true;
    }

    return postMonitor(r);
}

// Posts ONE monitor's own status under its own name. GEN2 auto-creates a
// probes row the first time it sees an unknown monitor name, which is exactly
// what makes a locally-added monitor appear on the GEN2 dashboard.
//
// Consequence worth knowing: once a monitor is visible in GEN2, an admin can
// dispatch a "remove" for it, and Gen2MonitorSync will honour that by
// deleting it locally.
bool Gen2Telemetry::postMonitor(const NetworkProbeResult &r) {
    const DeviceConfig &c = config_.get();

    JsonDocument doc;
    doc["license_key"] = c.gen2LicenseKey;
    doc["org_id"] = c.gen2OrgId;
    doc["monitor"] = r.label;
    doc["status"] = r.reachable ? "UP" : "DOWN";
    doc["server_id"] = device_.getDeviceId();
    doc["target"] = r.target;
    doc["latency_ms"] = r.latencyMs;

    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.setTimeout(c.probeTimeoutMs);

    SecureClient client;
    client.setInsecure(); // same trade-off as postEnvironment — see the header

    String host = extractHost(c.gen2ServerUrl);
#if defined(PLATFORM_ESP8266)
    if (SecureClient::probeMaxFragmentLength(host, 443, 1024)) {
        client.setBufferSizes(1024, 512);
    }
#endif

    String url = buildGen2Url(c.gen2ServerUrl);
    if (!http.begin(client, url)) {
        Logger::warn(TAG, "Failed to begin monitor beacon for " + r.label);
        return false;
    }
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.POST(body);
    http.end();

    if (httpCode == 429) {
        // GEN2's per-probe throttle: it wants this monitor reported less
        // often than we tried. Harmless — the next cycle will be in time.
        Logger::info(TAG, "Monitor " + r.label + " beacon throttled by GEN2 (429)");
        return false;
    }
    if (httpCode != 200) {
        Logger::warn(TAG, "Monitor " + r.label + " beacon returned HTTP " + String(httpCode));
        return false;
    }
    // Logged on success too: without this, "is this monitor reaching GEN2?"
    // is unanswerable from the serial log, since silence covers both "sent
    // fine" and "never attempted".
    Logger::info(TAG, "Monitor \"" + r.label + "\" published to GEN2 (" +
                          (r.reachable ? "UP" : "DOWN") + ")");
    return true;
}

bool Gen2Telemetry::publishDeviceStatus(const DeviceStatus &status) {
    (void)status;
    return true;
}

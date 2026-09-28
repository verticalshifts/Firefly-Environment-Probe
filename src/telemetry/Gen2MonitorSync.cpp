#include "Gen2MonitorSync.h"
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

static const char *TAG = "Gen2Sync";

// Strips exactly one trailing slash so "https://host/" + "/api/..." doesn't
// produce a double slash — same helper shape as Gen2Telemetry.cpp.
static String baseUrl(const String &serverUrl) {
    String base = serverUrl;
    if (base.endsWith("/")) base.remove(base.length() - 1);
    return base;
}

// License keys and org UUIDs are already URL-safe, but they're user-entered,
// so encode rather than trust that.
static String urlEncode(const String &s) {
    static const char *hex = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char ch = s[i];
        bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
                    ch == '.' || ch == '~';
        if (safe) {
            out += ch;
        } else {
            out += '%';
            out += hex[(uint8_t)ch >> 4];
            out += hex[(uint8_t)ch & 0x0F];
        }
    }
    return out;
}

// Reads a string field, returning "" rather than the literal "null" that a
// bare `| ""` would produce for a JSON null (GEN2 leaves target null on some
// remove jobs).
static String str(JsonObjectConst o, const char *key) {
    return o[key].is<const char *>() ? o[key].as<String>() : String("");
}

Gen2MonitorSync::Gen2MonitorSync(ConfigManager &config, NetworkManager &network)
    : config_(config), network_(network) {}

void Gen2MonitorSync::loop() {
    const DeviceConfig &c = config_.get();
    if (!c.gen2SyncEnabled) return;
    if (c.gen2LicenseKey.length() == 0 || c.gen2OrgId.length() == 0) return;
    if (!network_.isConnected()) return;

    unsigned long now = millis();
    uint32_t intervalMs = c.gen2SyncIntervalS * 1000UL;
    if (lastPollMs_ != 0 && (now - lastPollMs_ < intervalMs)) return;

    // Mark the attempt before making it, so a slow or failing poll can't turn
    // into a tight retry loop.
    lastPollMs_ = now;
    pollOnce();
}

bool Gen2MonitorSync::pollOnce() {
    const DeviceConfig &c = config_.get();

    String url = baseUrl(c.gen2ServerUrl) + "/api/groundprobe/jobs?license_key=" +
                 urlEncode(c.gen2LicenseKey) + "&org_id=" + urlEncode(c.gen2OrgId);

    HTTPClient http;
    http.setTimeout(c.probeTimeoutMs);

    WiFiClient plain;
    SecureClient secure;
    bool https = url.startsWith("https://");
    bool begun;
    if (https) {
        // Same setInsecure() trade-off as Gen2Telemetry — see its header for
        // why the certificate chain isn't verified on this hardware.
        secure.setInsecure();
        begun = http.begin(secure, url);
    } else {
        begun = http.begin(plain, url);
    }
    if (!begun) {
        Logger::warn(TAG, "Failed to begin jobs request");
        return false;
    }

    int code = http.GET();
    if (code != 200) {
        String detail = code > 0 ? http.getString() : http.errorToString(code);
        if (detail.length() > 120) detail = detail.substring(0, 120);
        Logger::warn(TAG, "Jobs poll returned " + String(code) + ": " + detail);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Logger::warn(TAG, String("Jobs response was not valid JSON: ") + err.c_str());
        return false;
    }
    if (!doc.is<JsonArray>()) {
        Logger::warn(TAG, "Jobs response was not an array");
        return false;
    }

    uint8_t applied = 0;
    for (JsonObjectConst job : doc.as<JsonArrayConst>()) {
        if (applied >= MAX_JOBS_PER_POLL) break;
        applied++;

        String jobId = str(job, "id");
        if (jobId.length() == 0) continue;

        bool handled = false;
        bool ok = applyJob(job, handled);

        // Ack when the job was applied, and also when it was one we can never
        // action (handled=false) — otherwise GEN2 would redeliver it forever.
        // A genuine apply FAILURE (ok=false, handled=true) is left unacked so
        // it comes back on the next poll.
        if (ok || !handled) {
            ackJob(jobId);
        }
    }

    if (applied > 0) Logger::info(TAG, "Processed " + String(applied) + " dispatched job(s)");
    return true;
}

bool Gen2MonitorSync::applyJob(JsonObjectConst job, bool &handled) {
    handled = true;

    String action = str(job, "action");
    String name = str(job, "monitor_name");
    if (name.length() == 0) {
        Logger::warn(TAG, "Job with no monitor_name, skipping");
        handled = false;
        return false;
    }

    if (action == "remove") {
        bool removed = config_.removeMonitorByName(name);
        Logger::info(TAG, removed ? ("Removed monitor \"" + name + "\" (GEN2)")
                                   : ("Monitor \"" + name + "\" already absent"));
        return true; // absent is the desired end state either way
    }

    if (action != "add") {
        Logger::warn(TAG, "Unknown job action \"" + action + "\", skipping");
        handled = false;
        return false;
    }

    String typeName = str(job, "monitor_type");
    if (typeName.length() == 0) typeName = "ping";
    MonitorType type;
    if (!monitorTypeFromName(typeName, type)) {
        Logger::warn(TAG, "Monitor \"" + name + "\" has unsupported type \"" + typeName + "\", skipping");
        handled = false;
        return false;
    }

    MonitorDef m;
    m.name = name;
    m.type = type;
    m.target = str(job, "target");
    m.gen2Owned = true;

    if (type == MonitorType::PORT) {
        // GEN2 has no separate port column — a port monitor's target carries
        // it as host:port.
        int colon = m.target.lastIndexOf(':');
        if (colon > 0) {
            long p = m.target.substring(colon + 1).toInt();
            if (p > 0 && p <= 65535) {
                m.port = (uint16_t)p;
                m.target = m.target.substring(0, colon);
            }
        }
        if (m.port == 0) {
            Logger::warn(TAG, "Port monitor \"" + name + "\" has no usable host:port target, skipping");
            handled = false;
            return false;
        }
    }

    String err;
    if (!config_.addOrUpdateMonitor(m, err)) {
        // A real failure (list full, validation) — left unacked so GEN2
        // redelivers it once the user has made room.
        Logger::warn(TAG, "Could not apply monitor \"" + name + "\": " + err);
        return false;
    }

    Logger::info(TAG, "Applied monitor \"" + name + "\" (" + typeName + ") from GEN2");
    return true;
}

bool Gen2MonitorSync::ackJob(const String &jobId) {
    const DeviceConfig &c = config_.get();

    String url = baseUrl(c.gen2ServerUrl) + "/api/groundprobe/jobs/" + urlEncode(jobId) +
                 "/ack?license_key=" + urlEncode(c.gen2LicenseKey) +
                 "&org_id=" + urlEncode(c.gen2OrgId);

    HTTPClient http;
    http.setTimeout(c.probeTimeoutMs);

    WiFiClient plain;
    SecureClient secure;
    bool https = url.startsWith("https://");
    bool begun;
    if (https) {
        secure.setInsecure();
        begun = http.begin(secure, url);
    } else {
        begun = http.begin(plain, url);
    }
    if (!begun) {
        Logger::warn(TAG, "Failed to begin ack request");
        return false;
    }

    http.addHeader("Content-Type", "application/json");
    int code = http.POST(String("{}"));
    http.end();

    if (code < 200 || code >= 300) {
        // 409 means GEN2 doesn't consider the job "delivered" any more, which
        // usually means another device claimed it — not something to retry.
        Logger::warn(TAG, "Ack for job " + jobId + " returned HTTP " + String(code));
        return false;
    }
    return true;
}

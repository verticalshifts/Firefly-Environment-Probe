#include "WebServerManager.h"
#include "hardware/HardwareConfig.h"
#include "hardware/Platform.h"
#include "hardware/FSCompat.h"
#include "ota/GitHubAssetRootCA.h"
#include "util/Logger.h"

#if defined(PLATFORM_ESP32)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
using SecureClient = WiFiClientSecure;
#elif defined(PLATFORM_ESP8266)
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <bearssl/bearssl_hash.h>
using SecureClient = BearSSL::WiFiClientSecure;
#endif

static const char *TAG = "WebServer";

// Bare host (scheme/path/port stripped) — same idiom as Gen2Telemetry.cpp /
// NetworkProbe.cpp, needed for ESP8266's probeMaxFragmentLength().
static String hostFromUrl(const String &url) {
    String host = url;
    int schemeEnd = host.indexOf("://");
    if (schemeEnd >= 0) host = host.substring(schemeEnd + 3);
    int pathStart = host.indexOf('/');
    if (pathStart >= 0) host = host.substring(0, pathStart);
    int portStart = host.indexOf(':');
    if (portStart >= 0) host = host.substring(0, portStart);
    return host;
}

static String toHex(const uint8_t *bytes, size_t len) {
    static const char *hexDigits = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += hexDigits[bytes[i] >> 4];
        out += hexDigits[bytes[i] & 0x0F];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Status classification helpers. Thresholds always come from config so the
// UI never hard-codes them (section 15).
// ---------------------------------------------------------------------------

static String classifyEnvironment(const EnvironmentReading &r, EnvironmentStatus status, const DeviceConfig &c) {
    if (status == EnvironmentStatus::SENSOR_ERROR) return "SENSOR_ERROR";
    if (status == EnvironmentStatus::NOT_YET_READ) return "OFFLINE";

    bool tempBad = r.temperature > c.tempHighC || r.temperature < c.tempLowC;
    bool humBad = r.humidity > c.humidityHighPct || r.humidity < c.humidityLowPct;
    if (!tempBad && !humBad) return "HEALTHY";

    float tempOverBy = max(r.temperature - c.tempHighC, c.tempLowC - r.temperature);
    float humOverBy = max(r.humidity - c.humidityHighPct, c.humidityLowPct - r.humidity);
    if (tempOverBy > 5.0f || humOverBy > 15.0f) return "CRITICAL";
    return "WARNING";
}

static String classifyProbeTarget(const NetworkProbeResult &r, const DeviceConfig &c) {
    if (!r.everRun) return "OFFLINE";
    if (!r.reachable) return "DOWN";
    if (r.packetLossPercent > c.packetLossHighPct || r.latencyMs > c.latencyHighMs) return "DEGRADED";
    return "UP";
}

static String classifyOverallNetwork(NetworkManager &net, NetworkProbe &probe, const DeviceConfig &c) {
    if (!net.isConnected()) return "OFFLINE";
    if (net.getRSSI() != 0 && net.getRSSI() < c.rssiLowDbm) return "WARNING";
    String gw = classifyProbeTarget(probe.result(ProbeId::GATEWAY), c);
    if (gw == "DOWN") return "CRITICAL";
    if (gw == "DEGRADED") return "WARNING";
    return "HEALTHY";
}

// ---------------------------------------------------------------------------

WebServerManager::WebServerManager(ConfigManager &config,
                                    EnvironmentManager &environment,
                                    NetworkManager &network,
                                    NetworkProbe &probe,
                                    DeviceManager &device,
                                    StorageManager &storage,
                                    BootGuard &bootGuard,
                                    OTAUpdateChecker &otaChecker)
    : server_(hw::HTTP_PORT),
      config_(config),
      environment_(environment),
      network_(network),
      probe_(probe),
      device_(device),
      storage_(storage),
      bootGuard_(bootGuard),
      otaChecker_(otaChecker) {}

void WebServerManager::begin() {
    setupRoutes();
    // Required: WebServer/ESP8266WebServer silently drop any request header
    // not explicitly opted into here — handleOtaUpload() reads X-File-Size
    // (section: OTA size-hint hardening). Passed as an already-decayed
    // pointer (not a bare array) so it binds to the (const char*[], size_t)
    // overload rather than ESP8266WebServer's newer variadic-template
    // collectHeaders(), which a raw array argument prefers via exact
    // reference binding and silently misinterprets as a list of header
    // names rather than an (array, count) pair.
    static const char *otaHeaderKeys[] = {"X-File-Size"};
    const char **otaHeaderKeysPtr = otaHeaderKeys;
    const size_t otaHeaderKeysCount = 1;
    server_.collectHeaders(otaHeaderKeysPtr, otaHeaderKeysCount);
    server_.begin();
    Logger::info(TAG, "Web server listening on port " + String(hw::HTTP_PORT));
}

void WebServerManager::loop() {
    server_.handleClient();
}

bool WebServerManager::requireAuth() {
    const DeviceConfig &c = config_.get();
    if (server_.authenticate(c.authUsername.c_str(), c.authPassword.c_str())) return true;
    server_.requestAuthentication(BASIC_AUTH, "Environment Probe");
    return false;
}

void WebServerManager::sendJson(int code, const JsonDocument &doc) {
    String body;
    serializeJson(doc, body);
    server_.send(code, "application/json", body);
}

void WebServerManager::sendError(int code, const String &message) {
    JsonDocument doc;
    doc["error"] = message;
    sendJson(code, doc);
}

String WebServerManager::contentTypeFor(const String &path) {
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".png")) return "image/png";
    return "text/plain";
}

bool WebServerManager::serveFile(String path) {
    if (path.endsWith("/")) path += "index.html";
    if (!storage_.exists(path)) return false;

    File f = LittleFS.open(path, "r");
    if (!f) return false;
    server_.streamFile(f, contentTypeFor(path));
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// Routing
// ---------------------------------------------------------------------------

void WebServerManager::setupRoutes() {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });

    server_.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    server_.on("/api/environment", HTTP_GET, [this]() { handleApiEnvironment(); });
    server_.on("/api/network", HTTP_GET, [this]() { handleApiNetwork(); });
    server_.on("/api/history", HTTP_GET, [this]() { handleApiHistory(); });

    server_.on("/api/config", HTTP_GET, [this]() { handleApiConfigGet(); });
    server_.on("/api/config", HTTP_POST, [this]() { handleApiConfigPost(); });

    server_.on("/api/restart", HTTP_POST, [this]() { handleApiRestart(); });
    server_.on("/api/factory-reset", HTTP_POST, [this]() { handleApiFactoryReset(); });

    // First-boot / re-provisioning Wi-Fi setup (used from the captive AP).
    server_.on("/provision", HTTP_POST, [this]() { handleProvisionSave(); });
    server_.on("/api/provisioning-info", HTTP_GET, [this]() { handleProvisioningInfo(); });

    server_.on(
        "/api/ota", HTTP_POST,
        [this]() { handleOtaComplete(); },
        [this]() { handleOtaUpload(); });

    server_.on("/api/ota/status", HTTP_GET, [this]() { handleApiOtaStatus(); });
    server_.on("/api/ota/check-now", HTTP_POST, [this]() { handleApiOtaCheckNow(); });
    server_.on("/api/ota/install-latest", HTTP_POST, [this]() { handleApiOtaInstallLatest(); });

    server_.onNotFound([this]() { handleNotFound(); });
}

void WebServerManager::handleRoot() {
    if (network_.isProvisioning()) {
        if (serveFile("/provision.html")) return;
        server_.send(200, "text/html", "<h1>Environment Probe Setup</h1><p>provision.html missing from filesystem.</p>");
        return;
    }
    if (serveFile("/index.html")) return;
    server_.send(200, "text/html", "<h1>Environment Probe</h1><p>Dashboard files missing — run 'pio run -t uploadfs'.</p>");
}

void WebServerManager::handleNotFound() {
    String path = server_.uri();
    if (serveFile(path)) return;

    if (network_.isProvisioning()) {
        // Captive portal behavior: any unknown URL while provisioning goes
        // to the setup page so phones/laptops surface the "sign in" prompt.
        server_.sendHeader("Location", "/", true);
        server_.send(302, "text/plain", "");
        return;
    }

    server_.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ---------------------------------------------------------------------------
// REST API — read endpoints (public on the LAN, no secrets in the payload)
// ---------------------------------------------------------------------------

void WebServerManager::handleApiStatus() {
    const DeviceConfig &c = config_.get();
    DeviceStatus dev = device_.getStatus();
    const EnvironmentReading &env = environment_.current();

    JsonDocument doc;
    JsonObject device = doc["device"].to<JsonObject>();
    device["id"] = dev.deviceId;
    device["name"] = dev.deviceName;
    device["platform"] = dev.platform;
    device["firmware"] = dev.firmwareVersion;
    device["uptimeSeconds"] = dev.uptimeS;
    device["freeHeap"] = dev.freeHeap;
    device["bootCount"] = device_.getBootCount();
    device["chipModel"] = PlatformManager::getChipModel();
    device["flashSize"] = PlatformManager::getFlashSize();
    device["macAddress"] = PlatformManager::getMacAddress();
    device["resetReason"] = PlatformManager::getResetReason();
    device["bootConfirmPending"] = bootGuard_.pendingConfirm();

    JsonObject environment = doc["environment"].to<JsonObject>();
    environment["temperature"] = env.temperature;
    environment["humidity"] = env.humidity;
    environment["sensorType"] = environment_.sensorType();
    environment["status"] = classifyEnvironment(env, environment_.status(), c);

    JsonObject network = doc["network"].to<JsonObject>();
    network["connected"] = network_.isConnected();
    network["ssid"] = network_.getSSID();
    network["ip"] = network_.getIPAddress();
    network["rssi"] = network_.getRSSI();
    network["status"] = classifyOverallNetwork(network_, probe_, c);
    network["provisioning"] = network_.isProvisioning();

    sendJson(200, doc);
}

void WebServerManager::handleApiEnvironment() {
    const DeviceConfig &c = config_.get();
    const EnvironmentReading &env = environment_.current();

    JsonDocument doc;
    doc["temperature"] = env.temperature;
    doc["humidity"] = env.humidity;
    doc["valid"] = env.valid;
    doc["sensorType"] = environment_.sensorType();
    doc["status"] = classifyEnvironment(env, environment_.status(), c);
    // env.timestamp is on the continuous-across-reboots clock (see
    // DeviceManager::getContinuousUptimeS()), not plain millis()/1000 — must
    // subtract against that same clock or this age is wrong by whatever the
    // persisted uptime offset is.
    doc["lastReadingAgeSeconds"] = env.valid ? (device_.getContinuousUptimeS() - env.timestamp) : (uint32_t)0;
    doc["thresholds"]["tempHighC"] = c.tempHighC;
    doc["thresholds"]["tempLowC"] = c.tempLowC;
    doc["thresholds"]["humidityHighPct"] = c.humidityHighPct;
    doc["thresholds"]["humidityLowPct"] = c.humidityLowPct;

    sendJson(200, doc);
}

void WebServerManager::handleApiNetwork() {
    const DeviceConfig &c = config_.get();

    JsonDocument doc;
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = network_.getSSID();
    wifi["ip"] = network_.getIPAddress();
    wifi["gateway"] = network_.getGatewayIP();
    wifi["rssi"] = network_.getRSSI();
    wifi["channel"] = network_.getChannel();
    wifi["connected"] = network_.isConnected();
    wifi["reconnectCount"] = network_.getReconnectCount();
    wifi["provisioning"] = network_.isProvisioning();
    wifi["apSsid"] = network_.getApSSID();

    JsonArray probes = doc["probes"].to<JsonArray>();
    ProbeId ids[] = {ProbeId::GATEWAY, ProbeId::PING_1, ProbeId::PING_2, ProbeId::DNS, ProbeId::HTTP};
    for (ProbeId id : ids) {
        const NetworkProbeResult &r = probe_.result(id);
        JsonObject o = probes.add<JsonObject>();
        o["label"] = r.label;
        o["target"] = r.target;
        o["reachable"] = r.reachable;
        o["latencyMs"] = r.latencyMs;
        o["packetLossPercent"] = r.packetLossPercent;
        o["lastProbeSecondsAgo"] = r.everRun ? (millis() / 1000 - r.timestamp) : (uint32_t)0;
        o["status"] = classifyProbeTarget(r, c);
        o["extra"] = r.extra;
    }

    sendJson(200, doc);
}

void WebServerManager::handleApiHistory() {
    uint32_t rangeSeconds = 3600;
    if (server_.hasArg("range")) {
        String range = server_.arg("range");
        if (range == "1h") rangeSeconds = 3600;
        else if (range == "6h") rangeSeconds = 6 * 3600;
        else if (range == "24h") rangeSeconds = 24 * 3600;
        else if (range == "7d") rangeSeconds = 7 * 24 * 3600;
    }

    // Static (not stack) buffer: keeps this bounded and avoids a large
    // stack allocation inside the web server's call chain (section 30).
    static EnvHistoryPoint points[HISTORY_MAX_OUTPUT_POINTS];
    uint32_t n = environment_.readHistoryRange(rangeSeconds, points, HISTORY_MAX_OUTPUT_POINTS);

    // Hand-built JSON, streamed in small chunks — deliberately not
    // ArduinoJson here, so a large history response never requires a large
    // temporary JSON document in RAM (section 30/34).
    server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server_.send(200, "application/json", "");

    server_.sendContent("{\"rangeSeconds\":" + String(rangeSeconds) + ",\"points\":[");
    char buf[64];
    for (uint32_t i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "%s{\"t\":%lu,\"temp\":%.1f,\"hum\":%.1f}",
                 i > 0 ? "," : "",
                 (unsigned long)points[i].timestamp,
                 points[i].temperature,
                 points[i].humidity);
        server_.sendContent(buf);
    }
    server_.sendContent("]}");
}

// ---------------------------------------------------------------------------
// REST API — configuration / control (auth required, section 25)
// ---------------------------------------------------------------------------

void WebServerManager::handleApiConfigGet() {
    if (!requireAuth()) return;
    JsonDocument doc;
    config_.toJson(doc, /*redactSecrets=*/true);
    sendJson(200, doc);
}

void WebServerManager::handleApiConfigPost() {
    if (!requireAuth()) return;

    if (!server_.hasArg("plain")) {
        sendError(400, "missing request body");
        return;
    }

    JsonDocument doc;
    DeserializationError parseErr = deserializeJson(doc, server_.arg("plain"));
    if (parseErr) {
        sendError(400, "invalid JSON");
        return;
    }

    bool wifiChanged = doc["wifiSsid"].is<const char *>();
    bool sensorChanged = doc["sensorType"].is<const char *>() || doc["sensorGpio"].is<int>();

    String err;
    if (!config_.update(doc.as<JsonObjectConst>(), err)) {
        sendError(400, err);
        return;
    }

    JsonDocument resp;
    resp["status"] = "ok";
    sendJson(200, resp);

    if (sensorChanged) environment_.reconfigure();
    if (wifiChanged) network_.applyNewCredentials();
}

void WebServerManager::handleApiRestart() {
    if (!requireAuth()) return;
    JsonDocument doc;
    doc["status"] = "restarting";
    sendJson(200, doc);
    restartRequested_ = true;
    restartAtMs_ = millis() + 750; // let the response flush first
}

void WebServerManager::handleApiFactoryReset() {
    if (!requireAuth()) return;
    JsonDocument doc;
    doc["status"] = "factory-reset";
    sendJson(200, doc);
    factoryResetRequested_ = true;
    restartRequested_ = true;
    restartAtMs_ = millis() + 750;
}

void WebServerManager::handleProvisionSave() {
    // Reachable only from the open provisioning AP, which by definition has
    // no prior credentials to authenticate against — this endpoint is only
    // meant to be exposed while network_.isProvisioning() is true.
    if (!network_.isProvisioning()) {
        sendError(403, "not in provisioning mode");
        return;
    }
    if (!server_.hasArg("plain")) {
        sendError(400, "missing request body");
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, server_.arg("plain"))) {
        sendError(400, "invalid JSON");
        return;
    }

    bool sensorChanged = doc["sensorType"].is<const char *>() || doc["sensorGpio"].is<int>();

    String err;
    if (!config_.update(doc.as<JsonObjectConst>(), err)) {
        sendError(400, err);
        return;
    }

    JsonDocument resp;
    resp["status"] = "ok";
    resp["deviceId"] = device_.getDeviceId();
    sendJson(200, resp);

    // The sensor was already begin()'d in setup() with whatever was in
    // config at boot (defaults, on first provisioning) — apply a
    // sensorType/sensorGpio change from the setup page immediately rather
    // than leaving the live sensor misconfigured until a reboot happens to
    // occur. Mirrors handleApiConfigPost()'s handling of the same fields.
    if (sensorChanged) environment_.reconfigure();
    network_.applyNewCredentials();
}

void WebServerManager::handleProvisioningInfo() {
    // Deliberately unauthenticated, but only ever answers while the device
    // is in the open provisioning AP — i.e. before it has joined a real
    // network, when only someone with physical/AP proximity could reach it.
    // This is how the auto-generated dashboard password (section 25) gets
    // handed to the person setting the device up.
    if (!network_.isProvisioning()) {
        sendError(403, "not in provisioning mode");
        return;
    }
    const DeviceConfig &c = config_.get();
    JsonDocument doc;
    doc["deviceId"] = device_.getDeviceId();
    doc["apSsid"] = network_.getApSSID();
    doc["dashboardUsername"] = c.authUsername;
    doc["dashboardPassword"] = c.authPassword;
    sendJson(200, doc);
}

// ---------------------------------------------------------------------------
// OTA (section 26)
// ---------------------------------------------------------------------------

void WebServerManager::handleOtaUpload() {
    HTTPUpload &upload = server_.upload();

    if (upload.status == UPLOAD_FILE_START) {
        // WebServer only calls the "main" handler after the upload body has
        // been fully consumed, so we authenticate as early as possible here
        // and simply decline to act on the bytes if it fails; the final
        // response (sent from handleOtaComplete) reflects the real outcome.
        otaAuthorized_ = requireAuth();
        if (!otaAuthorized_) return;

        Logger::info(TAG, "OTA upload started: " + upload.filename);
        // A multipart upload's Content-Length isn't visible to this
        // handler, so the browser sends the real file size in a custom
        // header instead (data/js/settings.js) — lets Update.begin() reject
        // an oversized image immediately instead of only failing mid-stream
        // once the inactive OTA partition fills up. Sanity-bounded rather
        // than trusted outright; falls back to "unknown size" mode (as
        // before) if the header is missing/implausible.
        size_t sizeHint = 0;
        String sizeHeader = server_.header("X-File-Size");
        if (sizeHeader.length() > 0) {
            long parsed = sizeHeader.toInt();
            if (parsed > 0 && parsed <= 4 * 1024 * 1024) sizeHint = (size_t)parsed;
        }
        otaInProgress_ = ota_.start(sizeHint);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (otaAuthorized_ && otaInProgress_) {
            if (!ota_.write(upload.buf, upload.currentSize)) {
                otaInProgress_ = false;
            }
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (otaAuthorized_ && otaInProgress_) {
            otaInProgress_ = ota_.finish();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaInProgress_ = false;
    }
}

void WebServerManager::handleOtaComplete() {
    if (!otaAuthorized_) {
        server_.requestAuthentication(BASIC_AUTH, "Environment Probe");
        return;
    }

    JsonDocument doc;
    if (otaInProgress_) {
        doc["status"] = "ok";
        sendJson(200, doc);
        bootGuard_.armPendingConfirm(""); // version unknown for a manual upload — diagnostic-only
        restartRequested_ = true;
        restartAtMs_ = millis() + 1000;
    } else {
        doc["status"] = "error";
        doc["message"] = ota_.lastError();
        sendJson(500, doc);
    }
    otaAuthorized_ = false;
    otaInProgress_ = false;
}

// ---------------------------------------------------------------------------
// OTA auto-update checking — opt-in, notify-only (src/ota/OTAUpdateChecker.h)
// ---------------------------------------------------------------------------

void WebServerManager::handleApiOtaStatus() {
    // No auth — same bar as /api/status/etc: nothing secret in this payload
    // (assetUrl/assetSha256 are public GitHub release metadata).
    const OtaUpdateInfo &info = otaChecker_.info();
    JsonDocument doc;
    doc["checked"] = info.checked;
    doc["available"] = info.available;
    doc["latestVersion"] = info.latestVersion;
    doc["currentVersion"] = FIRMWARE_VERSION;
    doc["assetUrl"] = info.assetUrl;
    doc["assetSha256"] = info.assetSha256;
    doc["releaseNotesUrl"] = info.releaseNotesUrl;
    doc["lastError"] = info.lastError;
    doc["lastCheckSecondsAgo"] = info.lastCheckMs == 0 ? -1 : (int)((millis() - info.lastCheckMs) / 1000);
    doc["bootConfirmPending"] = bootGuard_.pendingConfirm();
    doc["lastBootFailedToConfirm"] = bootGuard_.lastBootFailedToConfirm();
    sendJson(200, doc);
}

void WebServerManager::handleApiOtaCheckNow() {
    if (!requireAuth()) return;
    otaChecker_.checkNow();
    JsonDocument doc;
    doc["status"] = "ok";
    sendJson(200, doc);
}

void WebServerManager::handleApiOtaInstallLatest() {
    if (!requireAuth()) return;

    const OtaUpdateInfo &info = otaChecker_.info();
    if (!info.available) {
        sendError(400, "no update available");
        return;
    }
    if (info.assetSha256.length() == 0) {
        sendError(400, "no checksum published for this release — refusing to install unverified");
        return;
    }

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS); // github.com/.../download/... redirects to the CDN

    SecureClient client;
    // Different host, different pin than the version-check call
    // (OTAUpdateChecker.cpp) — the redirect target is GitHub's release-asset
    // CDN, not api.github.com. See GitHubAssetRootCA.h.
#if defined(PLATFORM_ESP32)
    client.setCACert(GITHUB_ASSET_ROOT_CA);
#elif defined(PLATFORM_ESP8266)
    static BearSSL::X509List assetRootCert(GITHUB_ASSET_ROOT_CA);
    client.setTrustAnchors(&assetRootCert);
    if (SecureClient::probeMaxFragmentLength(hostFromUrl(info.assetUrl), 443, 1024)) {
        client.setBufferSizes(1024, 512);
    }
#endif

    if (!http.begin(client, info.assetUrl)) {
        sendError(500, "failed to begin HTTPS connection to release asset");
        return;
    }

    int httpCode = http.GET();
    if (httpCode != 200) {
        http.end();
        sendError(502, "asset download failed (HTTP " + String(httpCode) + ")");
        return;
    }

    int size = http.getSize(); // Content-Length — a direct download, unlike the browser-upload path, so this is real
    if (!ota_.start(size > 0 ? (size_t)size : 0)) {
        http.end();
        sendError(500, "failed to start OTA write: " + ota_.lastError());
        return;
    }

    WiFiClient *stream = http.getStreamPtr();
#if defined(PLATFORM_ESP32)
    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts_ret(&shaCtx, 0);
#elif defined(PLATFORM_ESP8266)
    br_sha256_context shaCtx;
    br_sha256_init(&shaCtx);
#endif

    static uint8_t buf[512];
    int remaining = size; // -1 (unknown) just means "read until the stream closes"
    bool writeFailed = false;
    while (http.connected() && (remaining > 0 || remaining == -1)) {
        size_t available = stream->available();
        if (!available) {
            if (!http.connected()) break;
            delay(1);
            continue;
        }
        size_t toRead = available > sizeof(buf) ? sizeof(buf) : available;
        int got = stream->readBytes(buf, toRead);
        if (got <= 0) break;

        if (!ota_.write(buf, (size_t)got)) {
            writeFailed = true;
            break;
        }
#if defined(PLATFORM_ESP32)
        mbedtls_sha256_update_ret(&shaCtx, buf, (size_t)got);
#elif defined(PLATFORM_ESP8266)
        br_sha256_update(&shaCtx, buf, (size_t)got);
#endif
        if (remaining > 0) remaining -= got;
        PlatformManager::feedWatchdog();
        yield();
    }
    http.end();

    if (writeFailed || (remaining > 0)) {
        sendError(500, "download incomplete: " + ota_.lastError());
        return;
    }

    uint8_t digest[32];
#if defined(PLATFORM_ESP32)
    mbedtls_sha256_finish_ret(&shaCtx, digest);
#elif defined(PLATFORM_ESP8266)
    br_sha256_out(&shaCtx, digest);
#endif
    String computedSha = toHex(digest, sizeof(digest));

    // Checked BEFORE finish()/Update.end(true): the new image only lives in
    // the *inactive* partition until finish() flips the boot pointer, so a
    // mismatch here leaves the currently-running image completely
    // untouched. This ordering is the entire safety property — do not call
    // ota_.finish() before this check passes.
    if (!computedSha.equalsIgnoreCase(info.assetSha256)) {
        Logger::error(TAG, "OTA checksum mismatch — expected " + info.assetSha256 + " got " + computedSha);
        sendError(500, "checksum mismatch — aborted, device unchanged");
        return;
    }

    if (!ota_.finish()) {
        sendError(500, "OTA finalize failed: " + ota_.lastError());
        return;
    }

    JsonDocument doc;
    doc["status"] = "ok";
    sendJson(200, doc);
    bootGuard_.armPendingConfirm(info.latestVersion);
    restartRequested_ = true;
    restartAtMs_ = millis() + 1000;
}

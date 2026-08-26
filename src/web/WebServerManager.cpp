#include "WebServerManager.h"
#include "hardware/HardwareConfig.h"
#include "hardware/Platform.h"
#include "hardware/FSCompat.h"
#include "util/Logger.h"

static const char *TAG = "WebServer";

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
                                    StorageManager &storage)
    : server_(hw::HTTP_PORT),
      config_(config),
      environment_(environment),
      network_(network),
      probe_(probe),
      device_(device),
      storage_(storage) {}

void WebServerManager::begin() {
    setupRoutes();
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
    doc["lastReadingAgeSeconds"] = env.valid ? (millis() / 1000 - env.timestamp) : (uint32_t)0;
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
        // Total size isn't known up front for a multipart upload on either
        // platform's WebServer — OTAManager falls back to "unknown size"
        // mode, which both Update.h implementations support fine.
        otaInProgress_ = ota_.start(0);
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

#pragma once
// -----------------------------------------------------------------------------
// WebServerManager.h
//
// Local dashboard + REST API (sections 13-14, 16-17, 22, 25, 26). Serves
// data/ off LittleFS and exposes /api/*. Synchronous WebServer (not an async
// library) by design: fewer registry dependencies to break across ESP32/
// ESP8266, and every handler here does bounded, quick work so the server
// stays responsive without needing async I/O (section 10/43).
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "config/ConfigManager.h"
#include "environment/EnvironmentManager.h"
#include "network/NetworkManager.h"
#include "network/NetworkProbe.h"
#include "device/DeviceManager.h"
#include "storage/StorageManager.h"
#include "ota/OTAManager.h"

#if defined(PLATFORM_ESP32)
#include <WebServer.h>
using HttpServer = WebServer;
#elif defined(PLATFORM_ESP8266)
#include <ESP8266WebServer.h>
using HttpServer = ESP8266WebServer;
#endif

class WebServerManager {
public:
    WebServerManager(ConfigManager &config,
                      EnvironmentManager &environment,
                      NetworkManager &network,
                      NetworkProbe &probe,
                      DeviceManager &device,
                      StorageManager &storage);

    void begin();
    void loop();

    // True on the single loop() pass where a deferred restart/factory-reset
    // is due; main.cpp checks this so it can be certain nothing else runs
    // right before the reboot.
    bool restartPending() const { return restartRequested_ && millis() >= restartAtMs_; }
    bool factoryResetPending() const { return factoryResetRequested_; }

private:
    HttpServer server_;
    ConfigManager &config_;
    EnvironmentManager &environment_;
    NetworkManager &network_;
    NetworkProbe &probe_;
    DeviceManager &device_;
    StorageManager &storage_;
    OTAManager ota_;

    bool restartRequested_ = false;
    unsigned long restartAtMs_ = 0;
    bool factoryResetRequested_ = false;
    bool otaAuthorized_ = false;
    bool otaInProgress_ = false;

    static constexpr uint32_t HISTORY_MAX_OUTPUT_POINTS = 300;

    void setupRoutes();
    bool requireAuth();
    void sendJson(int code, const JsonDocument &doc);
    void sendError(int code, const String &message);
    String contentTypeFor(const String &path);
    bool serveFile(String path);

    void handleRoot();
    void handleApiStatus();
    void handleApiEnvironment();
    void handleApiNetwork();
    void handleApiHistory();
    void handleApiConfigGet();
    void handleApiConfigPost();
    void handleApiRestart();
    void handleApiFactoryReset();
    void handleProvisionSave();
    void handleProvisioningInfo();
    void handleOtaComplete();
    void handleOtaUpload();
    void handleNotFound();
};

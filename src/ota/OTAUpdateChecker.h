#pragma once
// -----------------------------------------------------------------------------
// OTAUpdateChecker.h
//
// Opt-in, disabled-by-default (config.otaCheckEnabled), notify-only update
// checking against this project's GitHub Releases
// (OTA_GITHUB_OWNER/OTA_GITHUB_REPO, compile-time constants — see
// platformio.ini). Never auto-installs: it just discovers a newer release
// and makes it visible via GET /api/ota/status; a human clicks "Install
// Update" in Settings to actually trigger the download+flash (see
// WebServerManager::handleApiOtaInstallLatest()).
//
// TLS is certificate-verified (src/ota/GitHubApiRootCA.h), NOT
// setInsecure() — unlike Gen2Telemetry's own tradeoff (justified there
// because a spoofed sensor reading is low-stakes), a MITM'd OTA
// version-check response could feed the device a malicious download URL,
// so this connection stays fully verified even at some latency cost.
//
// Checksum verification: the release description body is expected to
// contain plain-text lines "SHA256_ESP32: <hex>" / "SHA256_ESP8266: <hex>"
// — parsed here (from the same, already-pinned api.github.com response, no
// extra download/trust-chain needed) and enforced by the install endpoint,
// which refuses to proceed if no checksum was published. See
// docs/release-process.md for the release-authoring convention this
// depends on (asset naming, checksum lines).
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "config/ConfigManager.h"
#include "network/NetworkManager.h"

struct OtaUpdateInfo {
    bool checked = false;
    bool available = false;
    String latestVersion;   // e.g. "1.1.0" — leading 'v' stripped
    String assetUrl;        // browser_download_url for this platform's asset
    String assetSha256;     // "" if the release body has no checksum line
    String releaseNotesUrl; // html_url — surfaced so a human reads before installing
    String lastError;       // "" on last-check success
    unsigned long lastCheckMs = 0;
};

class OTAUpdateChecker {
public:
    OTAUpdateChecker(ConfigManager &config, NetworkManager &network);

    // Non-blocking millis()-gated tick from main.cpp's loop() — same shape
    // as Gen2Telemetry::publishEnvironment()'s interval gate.
    void loop();

    const OtaUpdateInfo &info() const { return info_; }

    // Settings "Check Now" button — makes the next loop() call check
    // immediately regardless of the interval.
    void checkNow() { forceCheck_ = true; }

private:
    ConfigManager &config_;
    NetworkManager &network_;
    OtaUpdateInfo info_;
    unsigned long lastCheckMs_ = 0;
    bool forceCheck_ = false;

    bool performCheck();

    // Strips a leading 'v', parses "major.minor.patch" as three integers.
    // Returns false (and leaves maj/min/patch untouched) on any format it
    // doesn't recognize — callers must treat that as "can't compare", never
    // as "is newer".
    static bool parseSemver(const String &s, int &maj, int &min, int &patch);
    static bool isNewer(const String &latest, const String &current);
};

#include "OTAUpdateChecker.h"
#include "GitHubApiRootCA.h"
#include "util/Logger.h"
#include <ArduinoJson.h>

#if defined(PLATFORM_ESP32)
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
using SecureClient = WiFiClientSecure;
static const char *PLATFORM_ASSET_NAME = "firmware-esp32.bin";
static const char *PLATFORM_SHA_KEY = "SHA256_ESP32:";
#elif defined(PLATFORM_ESP8266)
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
using SecureClient = BearSSL::WiFiClientSecure;
static const char *PLATFORM_ASSET_NAME = "firmware-esp8266.bin";
static const char *PLATFORM_SHA_KEY = "SHA256_ESP8266:";
#endif

static const char *TAG = "OTACheck";

OTAUpdateChecker::OTAUpdateChecker(ConfigManager &config, NetworkManager &network)
    : config_(config), network_(network) {}

void OTAUpdateChecker::loop() {
    const DeviceConfig &c = config_.get();
    if (!c.otaCheckEnabled) return;

    unsigned long now = millis();
    uint32_t intervalMs = c.otaCheckIntervalS * 1000UL;
    bool due = forceCheck_ || lastCheckMs_ == 0 || (now - lastCheckMs_ >= intervalMs);
    if (!due) return;

    if (!network_.isConnected()) return;

    lastCheckMs_ = now;
    forceCheck_ = false;
    performCheck();
}

bool OTAUpdateChecker::parseSemver(const String &s, int &maj, int &min, int &patch) {
    String v = s;
    if (v.startsWith("v") || v.startsWith("V")) v.remove(0, 1);

    int dot1 = v.indexOf('.');
    if (dot1 < 0) return false;
    int dot2 = v.indexOf('.', dot1 + 1);
    if (dot2 < 0) return false;

    String majS = v.substring(0, dot1);
    String minS = v.substring(dot1 + 1, dot2);
    String patchS = v.substring(dot2 + 1);
    // patchS may have trailing text (e.g. "0-rc1") — take the leading digits only.
    int i = 0;
    while (i < (int)patchS.length() && isDigit(patchS[i])) i++;
    patchS = patchS.substring(0, i);

    if (majS.length() == 0 || minS.length() == 0 || patchS.length() == 0) return false;
    for (char ch : majS) if (!isDigit(ch)) return false;
    for (char ch : minS) if (!isDigit(ch)) return false;

    maj = majS.toInt();
    min = minS.toInt();
    patch = patchS.toInt();
    return true;
}

bool OTAUpdateChecker::isNewer(const String &latest, const String &current) {
    int lMaj, lMin, lPatch, cMaj, cMin, cPatch;
    if (!parseSemver(latest, lMaj, lMin, lPatch)) return false;
    if (!parseSemver(current, cMaj, cMin, cPatch)) return false;

    if (lMaj != cMaj) return lMaj > cMaj;
    if (lMin != cMin) return lMin > cMin;
    return lPatch > cPatch;
}

bool OTAUpdateChecker::performCheck() {
    HTTPClient http;
    SecureClient client;
#if defined(PLATFORM_ESP32)
    // ESP32 has ample RAM/CPU for this fast ECDSA chain — no MFLN needed.
    client.setCACert(GITHUB_API_ROOT_CA);
#elif defined(PLATFORM_ESP8266)
    static BearSSL::X509List rootCert(GITHUB_API_ROOT_CA);
    client.setTrustAnchors(&rootCert);
    // Same ESP8266 heap constraint proven repeatedly this session
    // (Gen2Telemetry.cpp, NetworkProbe.cpp): BearSSL's default 16KB+512B
    // buffer is too large a contiguous allocation to reliably fit this
    // device's free heap. api.github.com is expected to support MFLN.
    if (SecureClient::probeMaxFragmentLength("api.github.com", 443, 1024)) {
        client.setBufferSizes(1024, 512);
    }
#endif

    String url = String("https://api.github.com/repos/") + OTA_GITHUB_OWNER + "/" + OTA_GITHUB_REPO + "/releases/latest";
    if (!http.begin(client, url)) {
        info_.checked = true;
        info_.lastError = "failed to begin HTTPS connection";
        Logger::warn(TAG, info_.lastError);
        return false;
    }
    // GitHub's API 403s any request with no User-Agent — easy to miss.
    http.addHeader("User-Agent", String("FireflyEnvironmentProbe/") + FIRMWARE_VERSION);
    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("X-GitHub-Api-Version", "2022-11-28");

    int httpCode = http.GET();
    info_.checked = true;
    info_.lastCheckMs = millis();

    if (httpCode == 404) {
        info_.available = false;
        info_.lastError = "no releases found (repo private or empty)";
        http.end();
        Logger::info(TAG, info_.lastError);
        return true;
    }
    if (httpCode != 200) {
        info_.available = false;
        info_.lastError = "HTTP " + String(httpCode);
        http.end();
        Logger::warn(TAG, "Update check failed: " + info_.lastError);
        return false;
    }

    // Keep only the fields we use — GitHub's full release payload has
    // verbose nested author/uploader objects ESP8266 can't spare RAM for.
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["body"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        info_.available = false;
        info_.lastError = String("JSON parse failed: ") + err.c_str();
        Logger::warn(TAG, info_.lastError);
        return false;
    }

    String tagName = doc["tag_name"] | "";
    String version = tagName;
    if (version.startsWith("v") || version.startsWith("V")) version.remove(0, 1);

    String assetUrl;
    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        if (String(asset["name"].as<const char *>()) == PLATFORM_ASSET_NAME) {
            assetUrl = asset["browser_download_url"].as<const char *>();
            break;
        }
    }

    if (tagName.length() == 0 || assetUrl.length() == 0) {
        info_.available = false;
        info_.lastError = assetUrl.length() == 0
            ? String("no ") + PLATFORM_ASSET_NAME + " asset in latest release"
            : "release has no tag_name";
        Logger::warn(TAG, info_.lastError);
        return true; // not a transport failure — just nothing installable found
    }

    String body = doc["body"] | "";
    String sha256;
    int shaIdx = body.indexOf(PLATFORM_SHA_KEY);
    if (shaIdx >= 0) {
        int start = shaIdx + strlen(PLATFORM_SHA_KEY);
        while (start < (int)body.length() && isSpace(body[start])) start++;
        int end = start;
        while (end < (int)body.length() && isHexadecimalDigit(body[end])) end++;
        sha256 = body.substring(start, end);
        sha256.toLowerCase();
    }

    info_.latestVersion = version;
    info_.assetUrl = assetUrl;
    info_.assetSha256 = sha256;
    info_.releaseNotesUrl = doc["html_url"] | "";
    info_.available = isNewer(version, FIRMWARE_VERSION);
    info_.lastError = "";

    if (info_.available) {
        Logger::info(TAG, "Update available: " + version + " (current " + FIRMWARE_VERSION + ")" +
                               (sha256.length() == 0 ? " [no checksum published — install will be refused]" : ""));
    } else {
        Logger::info(TAG, "Up to date (latest release: " + tagName + ")");
    }
    return true;
}

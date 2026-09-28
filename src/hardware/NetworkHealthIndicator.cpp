#include "NetworkHealthIndicator.h"
#include "PingCompat.h"
#include "util/Logger.h"

static const char *TAG = "NetLED";
static const IPAddress PING_TARGET(8, 8, 8, 8);

NetworkHealthIndicator::NetworkHealthIndicator(uint8_t gpio, NetworkManager &network)
    : gpio_(gpio), network_(network) {}

void NetworkHealthIndicator::attachRgb(uint8_t rPin, uint8_t gPin, uint8_t bPin, bool commonAnode,
                                      uint8_t brightnessPct) {
    rgbR_ = rPin;
    rgbG_ = gPin;
    rgbB_ = bPin;
    rgbCommonAnode_ = commonAnode;
    rgbBrightnessPct_ = brightnessPct > 100 ? 100 : brightnessPct;
}

void NetworkHealthIndicator::begin() {
    pinMode(gpio_, OUTPUT);
    if (rgbR_ != NO_PIN) {
#if defined(PLATFORM_ESP32)
        ledcSetup(RGB_CH_R, RGB_PWM_FREQ, RGB_PWM_BITS);
        ledcSetup(RGB_CH_G, RGB_PWM_FREQ, RGB_PWM_BITS);
        ledcSetup(RGB_CH_B, RGB_PWM_FREQ, RGB_PWM_BITS);
        ledcAttachPin(rgbR_, RGB_CH_R);
        ledcAttachPin(rgbG_, RGB_CH_G);
        ledcAttachPin(rgbB_, RGB_CH_B);
#else
        pinMode(rgbR_, OUTPUT);
        pinMode(rgbG_, OUTPUT);
        pinMode(rgbB_, OUTPUT);
#endif
        rgbSelfTest(); // POST-style lamp check: R, G, B in turn, twice
    }
    setLed(true);   // steady-on is the default/fallback state — see header comment
    applyRgb();     // green (STEADY_ON) is the matching default colour
}

// Boot-time RGB lamp check: light red, then green, then blue on their own,
// for RGB_SELFTEST_CYCLES passes, so a dead channel or a mis-wired colour
// leg is obvious before the LED settles into its network-health colour.
// Blocking (called only from begin(), during setup()) and short by design.
void NetworkHealthIndicator::rgbSelfTest() {
    if (rgbR_ == NO_PIN) return;
    for (uint8_t cycle = 0; cycle < RGB_SELFTEST_CYCLES; cycle++) {
        const bool steps[3][3] = {{true, false, false}, {false, true, false}, {false, false, true}};
        for (auto &s : steps) {
            writeRgb(s[0], s[1], s[2]);
            delay(RGB_SELFTEST_ON_MS);
            writeRgb(false, false, false);
            delay(RGB_SELFTEST_GAP_MS);
        }
    }
}

PingHealthZone NetworkHealthIndicator::classify(bool ok, float latencyMs) {
    if (!ok || latencyMs > DEGRADED_MAX_MS) return PingHealthZone::BAD;
    if (latencyMs > GOOD_MAX_MS) return PingHealthZone::DEGRADED;
    return PingHealthZone::GOOD;
}

void NetworkHealthIndicator::loop() {
    unsigned long now = millis();

    if (!network_.isConnected()) {
        if (mode_ != NetworkLedMode::DISCONNECTED) {
            mode_ = NetworkLedMode::DISCONNECTED;
            phaseStartMs_ = now;
            // Reset the streak so a stale pre-disconnect run of bad/degraded
            // pings can't immediately show a degraded pattern the instant
            // Wi-Fi comes back, before any fresh pings have actually run.
            streakZone_ = PingHealthZone::GOOD;
            streakCount_ = 0;
            lastPingMs_ = 0; // ping right away on reconnect, don't wait out the old interval
            Logger::info(TAG, "Mode -> disconnected (no Wi-Fi)");
        }
        applyPattern();
        return;
    }

    if (lastPingMs_ == 0 || now - lastPingMs_ >= PING_INTERVAL_MS) {
        lastPingMs_ = now;

        float latencyMs = 0, lossPct = 0;
        bool ok = pingcompat::ping(PING_TARGET, 1, latencyMs, lossPct);
        PingHealthZone zone = classify(ok, latencyMs);

        if (zone == streakZone_) {
            if (streakCount_ < 255) streakCount_++;
        } else {
            streakZone_ = zone;
            streakCount_ = 1;
        }

        NetworkLedMode newMode = NetworkLedMode::STEADY_ON; // default/fallback
        if (streakZone_ == PingHealthZone::BAD && streakCount_ >= CONSECUTIVE_THRESHOLD) {
            newMode = NetworkLedMode::FAST_BLINK;
        } else if (streakZone_ == PingHealthZone::DEGRADED && streakCount_ >= CONSECUTIVE_THRESHOLD) {
            newMode = NetworkLedMode::SLOW_BLINK;
        }

        if (newMode != mode_) {
            mode_ = newMode;
            phaseStartMs_ = now; // restart the on/off cycle cleanly on every mode change
            const char *modeName = mode_ == NetworkLedMode::STEADY_ON ? "steady-on"
                                  : mode_ == NetworkLedMode::SLOW_BLINK ? "slow-blink (60-90ms)"
                                                                        : "fast-blink (>90ms/lost)";
            Logger::info(TAG, String("Mode -> ") + modeName + " (last ping " +
                                   (ok ? String(latencyMs, 0) + "ms" : String("lost")) + ")");
        }
    }

    applyPattern();
}

void NetworkHealthIndicator::applyPattern() {
    applyRgb(); // keep the RGB colour in lock-step with the mono LED's mode

    switch (mode_) {
        case NetworkLedMode::STEADY_ON:
            setLed(true);
            return;
        case NetworkLedMode::SLOW_BLINK: {
            unsigned long pos = (millis() - phaseStartMs_) % (SLOW_ON_MS + SLOW_OFF_MS);
            setLed(pos < SLOW_ON_MS);
            return;
        }
        case NetworkLedMode::FAST_BLINK: {
            unsigned long pos = (millis() - phaseStartMs_) % (FAST_ON_MS + FAST_OFF_MS);
            setLed(pos < FAST_ON_MS);
            return;
        }
        case NetworkLedMode::DISCONNECTED: {
            unsigned long pos = (millis() - phaseStartMs_) % (DISCONNECTED_ON_MS + DISCONNECTED_OFF_MS);
            setLed(pos < DISCONNECTED_ON_MS);
            return;
        }
    }
}

void NetworkHealthIndicator::setLed(bool on) {
    digitalWrite(gpio_, on ? HIGH : LOW);
}

void NetworkHealthIndicator::applyRgb() {
    if (rgbR_ == NO_PIN) return;
    switch (mode_) {
        case NetworkLedMode::DISCONNECTED: writeRgb(false, false, true);  return; // blue
        case NetworkLedMode::STEADY_ON:    writeRgb(false, true,  false); return; // green
        case NetworkLedMode::SLOW_BLINK:   writeRgb(true,  true,  false); return; // amber
        case NetworkLedMode::FAST_BLINK:   writeRgb(true,  false, false); return; // red
    }
}

void NetworkHealthIndicator::writeRgb(bool r, bool g, bool b) {
#if defined(PLATFORM_ESP32)
    // 8-bit LEDC duty for a lit channel at the configured brightness; an
    // unlit channel is 0. Common-anode inverts (255 - duty) since the
    // channel is then pulled LOW to light it.
    const uint32_t lit = (rgbBrightnessPct_ * 255u + 50u) / 100u;
    auto duty = [this, lit](bool on) -> uint32_t {
        uint32_t d = on ? lit : 0u;
        return rgbCommonAnode_ ? (255u - d) : d;
    };
    ledcWrite(RGB_CH_R, duty(r));
    ledcWrite(RGB_CH_G, duty(g));
    ledcWrite(RGB_CH_B, duty(b));
#else
    // ESP8266: on/off only — no analogWrite here (see header note).
    digitalWrite(rgbR_, (r != rgbCommonAnode_) ? HIGH : LOW);
    digitalWrite(rgbG_, (g != rgbCommonAnode_) ? HIGH : LOW);
    digitalWrite(rgbB_, (b != rgbCommonAnode_) ? HIGH : LOW);
#endif
}

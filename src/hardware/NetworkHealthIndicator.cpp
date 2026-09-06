#include "NetworkHealthIndicator.h"
#include "PingCompat.h"
#include "util/Logger.h"

static const char *TAG = "NetLED";
static const IPAddress PING_TARGET(8, 8, 8, 8);

NetworkHealthIndicator::NetworkHealthIndicator(uint8_t gpio, NetworkManager &network)
    : gpio_(gpio), network_(network) {}

void NetworkHealthIndicator::begin() {
    pinMode(gpio_, OUTPUT);
    setLed(true); // steady-on is the default/fallback state — see header comment
}

PingHealthZone NetworkHealthIndicator::classify(bool ok, float latencyMs) {
    if (!ok || latencyMs > DEGRADED_MAX_MS) return PingHealthZone::BAD;
    if (latencyMs > GOOD_MAX_MS) return PingHealthZone::DEGRADED;
    return PingHealthZone::GOOD;
}

void NetworkHealthIndicator::loop() {
    unsigned long now = millis();

    if (network_.isConnected() && (lastPingMs_ == 0 || now - lastPingMs_ >= PING_INTERVAL_MS)) {
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
    }
}

void NetworkHealthIndicator::setLed(bool on) {
    digitalWrite(gpio_, on ? HIGH : LOW);
}

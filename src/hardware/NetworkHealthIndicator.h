#pragma once
// -----------------------------------------------------------------------------
// NetworkHealthIndicator.h
//
// Drives an LED on hw::DEFAULT_NETWORK_LED_GPIO to reflect ping latency to
// a fixed target (8.8.8.8) — repurposes the GPIO that TemperatureIndicator
// used to own (this firmware only has the one spare indicator LED wired
// up). Pattern:
//
//   Wi-Fi not connected                     1s on / 5s off
//   latency <= 59ms                         steady on
//   60-90ms, sustained 10 consecutive pings  3s on / 1s off
//   >90ms (or lost), sustained 10 consecutive pings  0.5s on / 0.5s off
//
// The disconnected pattern takes priority over everything else — no pings
// are possible without Wi-Fi, so it's checked first, independent of the
// consecutive-ping streak machinery below. Reconnecting resets the streak
// (see loop()) so a stale pre-disconnect streak can't immediately show a
// degraded pattern before any fresh pings have actually run.
//
// "Sustained 10 consecutive" is a real consecutive-run counter, not a
// sliding window — matches "for 10 consecutive pings" literally, and means
// a single stray slow/lost ping can't flip the LED into a degraded pattern
// on its own. Steady-on is the default/fallback state: it's whatever the
// LED shows whenever the last 10 pings *aren't* all-degraded or all-bad
// (recovery is immediate, no consecutive-good requirement — the spec only
// attaches a consecutive count to the two worse tiers).
//
// Ping target and cadence are fixed, not user-configurable: this pings
// 8.8.8.8 specifically (independent of the user-configurable pingTarget1
// used elsewhere by NetworkProbe, even though that defaults to the same
// IP) on its own 10s timer, separate from NetworkProbe's own 30s ground-
// probe cycle — getting 10 consecutive samples on a shared 30s cycle would
// take 5 minutes to react, far too slow for a status LED.
//
// A lost/timed-out ping via pingcompat::ping() blocks for the underlying
// library's fixed ~1s timeout (neither ESP32Ping nor ESP8266Ping expose a
// shorter one) — same bounded-blocking tradeoff docs/architecture.md
// already documents for NetworkProbe's own pings, just on a different
// (10s, not 30s) schedule. Worst case (100% loss) is a ~1s block once per
// 10s tick, not back-to-back with anything else on its own timer.
//
// Full brightness only (plain digitalWrite, no PWM/analogWrite) — ESP8266
// has no hardware PWM, and switching this exact LED to analogWrite for
// brightness control was confirmed live, earlier, to cause real WiFi
// packet loss (see git history / docs/hardware.md). Not worth reintroducing
// that risk for an LED that's now binary (on/off), not dimmed.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "network/NetworkManager.h"

enum class PingHealthZone { GOOD, DEGRADED, BAD };
enum class NetworkLedMode { STEADY_ON, SLOW_BLINK, FAST_BLINK, DISCONNECTED };

class NetworkHealthIndicator {
public:
    NetworkHealthIndicator(uint8_t gpio, NetworkManager &network);

    void begin();
    void loop();

private:
    uint8_t gpio_;
    NetworkManager &network_;

    static constexpr uint32_t PING_INTERVAL_MS = 10000; // 10s between pings
    static constexpr uint8_t CONSECUTIVE_THRESHOLD = 10;
    static constexpr float GOOD_MAX_MS = 59.0f;
    static constexpr float DEGRADED_MAX_MS = 90.0f;
    static constexpr unsigned long SLOW_ON_MS = 3000, SLOW_OFF_MS = 1000;
    static constexpr unsigned long FAST_ON_MS = 500, FAST_OFF_MS = 500;
    static constexpr unsigned long DISCONNECTED_ON_MS = 1000, DISCONNECTED_OFF_MS = 5000;

    unsigned long lastPingMs_ = 0;
    PingHealthZone streakZone_ = PingHealthZone::GOOD;
    uint8_t streakCount_ = 0;

    NetworkLedMode mode_ = NetworkLedMode::STEADY_ON;
    unsigned long phaseStartMs_ = 0;

    static PingHealthZone classify(bool ok, float latencyMs);
    void applyPattern();
    void setLed(bool on);
};

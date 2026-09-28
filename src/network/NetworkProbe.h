#pragma once
// -----------------------------------------------------------------------------
// NetworkProbe.h
//
// The ESP itself acting as a network ground probe (section 18). Runs the
// user's monitor list (src/network/MonitorDef.h) — ping, DNS, HTTP/HTTPS and
// TCP port checks — rather than the fixed five it used to hardcode.
//
// Non-blocking design note: each individual probe call is a bounded, short
// (probeTimeoutMs-ish) blocking operation — the underlying Arduino/lwIP APIs
// (ping, DNS resolution, TCP connect) don't offer a fully async surface on
// either platform without pulling in an async networking library. To keep
// the web server responsive, loop() runs AT MOST ONE probe per call, round-
// robin across monitors, so a stall is bounded to a single probe's timeout
// rather than all probes back-to-back. That bound matters more now that the
// number of monitors is user-controlled.
//
// Results are matched to their definition by `monitorId`, never by array
// index: monitors can be added or removed at runtime, and deleting one from
// the middle of the list must not slide another monitor's history onto it.
// loop() notices ConfigManager::monitorsGeneration() changing and rebuilds.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <IPAddress.h>
#include "config/ConfigManager.h"
#include "network/MonitorDef.h"
#include "network/NetworkManager.h"

struct NetworkProbeResult {
    String monitorId;       // MonitorDef::id this result belongs to
    String label;           // monitor name, copied for rendering convenience
    String target;
    bool reachable = false;
    bool everRun = false;
    float latencyMs = 0.0f;
    float packetLossPercent = 0.0f;
    uint32_t timestamp = 0; // seconds since boot
    String extra;           // e.g. resolved IP, HTTP status code, "open"
};

class NetworkProbe {
public:
    NetworkProbe(ConfigManager &config, NetworkManager &network);

    void loop();

    // Live results, in the same order as DeviceConfig::monitors.
    size_t count() const { return count_; }
    const NetworkProbeResult &result(size_t i) const { return results_[i]; }

    // Contiguous view of the same results, for callers shaped
    // (array, count) — notably TelemetryProvider::publishNetwork().
    const NetworkProbeResult *results() const { return results_; }

private:
    ConfigManager &config_;
    NetworkManager &network_;

    NetworkProbeResult results_[hw::MAX_MONITORS];
    unsigned long lastRunMs_[hw::MAX_MONITORS] = {0};
    size_t count_ = 0;
    size_t cursor_ = 0;

    // Sentinel: guarantees the first loop() call syncs, whatever generation
    // ConfigManager starts at.
    uint32_t generation_ = 0xFFFFFFFFu;

    // Rebuilds results_/lastRunMs_ from the config's monitor list, carrying
    // over each monitor's existing result by id.
    void syncWithConfig();

    void runProbe(size_t idx, const MonitorDef &m);
    void probePing(NetworkProbeResult &r, const String &target);
    void probeDns(NetworkProbeResult &r, const String &domain);
    void probeHttp(NetworkProbeResult &r, const String &url);
    void probePort(NetworkProbeResult &r, const String &host, uint16_t port);
};

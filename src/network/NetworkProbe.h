#pragma once
// -----------------------------------------------------------------------------
// NetworkProbe.h
//
// The ESP itself acting as a network ground probe (section 18): gateway
// reachability, two configurable IP ping targets, DNS resolution, and an
// HTTP/HTTPS endpoint check.
//
// Non-blocking design note: each individual probe call is a bounded, short
// (probeTimeoutMs-ish) blocking operation — the underlying Arduino/lwIP APIs
// (ping, DNS resolution, TCP connect) don't offer a fully async surface on
// either platform without pulling in an async networking library. To keep
// the web server responsive, loop() runs AT MOST ONE probe per call, round-
// robin across targets, so a stall is bounded to a single probe's timeout
// rather than all probes back-to-back.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <IPAddress.h>
#include "config/ConfigManager.h"
#include "network/NetworkManager.h"

enum class ProbeId : uint8_t { GATEWAY = 0, PING_1, PING_2, DNS, HTTP, COUNT };

struct NetworkProbeResult {
    String label;
    String target;
    bool reachable = false;
    bool everRun = false;
    float latencyMs = 0.0f;
    float packetLossPercent = 0.0f;
    uint32_t timestamp = 0; // seconds since boot
    String extra;           // e.g. resolved IP, HTTP status code
};

class NetworkProbe {
public:
    NetworkProbe(ConfigManager &config, NetworkManager &network);

    void loop();

    const NetworkProbeResult &result(ProbeId id) const { return results_[(uint8_t)id]; }

private:
    ConfigManager &config_;
    NetworkManager &network_;

    NetworkProbeResult results_[(uint8_t)ProbeId::COUNT];
    unsigned long lastRunMs_[(uint8_t)ProbeId::COUNT] = {0};
    uint8_t cursor_ = 0;

    void runProbe(ProbeId id);
    void probeGateway(NetworkProbeResult &r);
    void probePing(NetworkProbeResult &r, const String &target);
    void probeDns(NetworkProbeResult &r);
    void probeHttp(NetworkProbeResult &r);
};

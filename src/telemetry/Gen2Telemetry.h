#pragma once
// -----------------------------------------------------------------------------
// Gen2Telemetry.h
//
// TelemetryProvider implementation that POSTs environment (temperature/
// humidity) readings to GEN2 Bullseye's live groundprobe endpoint
// (https://g2i.batbapps.com/groundprobe). Opt-in via config (`gen2Enabled`,
// default false) — a device with default config still makes zero GEN2
// calls, so Phase 1's original "no GEN2 dependency" property holds for
// anyone who doesn't turn this on. See docs/architecture.md's "Phase 2
// boundary" section for the full story.
//
// publishNetwork()/publishDeviceStatus() are deliberately no-ops: GEN2's
// groundprobe endpoint has no temperature/humidity support server-side
// today (confirmed by reading GEN2's own server/routes.ts and
// server/storage.ts), let alone a distinct network-probe or device-status
// shape — only environment telemetry is in scope here. Any extra JSON keys
// this sends (temperature/humidity) are currently silently dropped by
// GEN2's backend; that's intentional future-proofing on the firmware side,
// not a bug — see docs/configuration.md.
// -----------------------------------------------------------------------------

#include "TelemetryProvider.h"
#include "config/ConfigManager.h"
#include "network/NetworkManager.h"
#include "device/DeviceManager.h"

class Gen2Telemetry : public TelemetryProvider {
public:
    Gen2Telemetry(ConfigManager &config, NetworkManager &network, DeviceManager &device);

    bool publishEnvironment(const EnvironmentReading &reading, const char *sensorType) override;

    // Out of scope for now — see class comment.
    bool publishNetwork(const NetworkProbeResult results[], size_t count) override;
    bool publishDeviceStatus(const DeviceStatus &status) override;

private:
    ConfigManager &config_;
    NetworkManager &network_;
    DeviceManager &device_;

    unsigned long lastPostMs_ = 0;
    float lastLatencyMs_ = 0.0f; // RTT of the *previous* GEN2 POST — see .cpp

    bool postEnvironment(const EnvironmentReading &reading);
};

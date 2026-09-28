#pragma once
// -----------------------------------------------------------------------------
// IotGatewayTelemetry.h
//
// TelemetryProvider that POSTs environment readings to a Firefly-CAP100 IoT
// Gateway (HUB > IoT Gateway), e.g.
//
//   POST http://192.168.0.24/ajax/iotgw/ingest.php
//   Authorization: Bearer iot_...
//   {"temperature": 24.1, "humidity": 51}
//
// A SECONDARY path: it runs alongside Gen2Telemetry, never instead of it.
// Both are independently opt-in, so a device can publish direct-to-GEN2, via
// the gateway, or both. The gateway validates, buffers, aggregates and
// forwards to GEN2 on its own schedule — so enabling both on one device with
// the gateway's GEN2 forwarding on would report the same readings to GEN2
// twice (once direct, once via the gateway). That's a deployment choice, not
// something this firmware can see or prevent.
//
// Payload notes:
// - `water_leak` is deliberately never sent. This probe has no leak sensor,
//   and the gateway repeats a sensor's last *reported* leak state for windows
//   without one — so sending a hard-coded `false` would claim a reading that
//   was never taken (and could resolve a real alarm raised by another source).
// - Readings are only published while the sensor is healthy (main.cpp gates
//   on EnvironmentStatus::OK). The gateway contract has no status field, so
//   posting EnvironmentManager's last-known-good value during a sensor fault
//   would silently report stale data as live.
//
// Transport: plain http:// is the expected case (LAN-only, token-protected
// endpoint). https:// is also accepted, without certificate verification —
// same setInsecure() trade-off as Gen2Telemetry.h, for the same reason.
// -----------------------------------------------------------------------------

#include "TelemetryProvider.h"
#include "config/ConfigManager.h"
#include "network/NetworkManager.h"

class IotGatewayTelemetry : public TelemetryProvider {
public:
    IotGatewayTelemetry(ConfigManager &config, NetworkManager &network);

    bool publishEnvironment(const EnvironmentReading &reading, const char *sensorType) override;

    // The gateway ingests environment readings only.
    bool publishNetwork(const NetworkProbeResult results[], size_t count) override;
    bool publishDeviceStatus(const DeviceStatus &status) override;

private:
    ConfigManager &config_;
    NetworkManager &network_;

    unsigned long lastPostMs_ = 0;

    bool postEnvironment(const EnvironmentReading &reading);
};

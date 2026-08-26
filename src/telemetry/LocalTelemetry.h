#pragma once
// -----------------------------------------------------------------------------
// LocalTelemetry.h
//
// Phase 1's only TelemetryProvider. Environment/network data is already
// persisted locally by EnvironmentManager/NetworkProbe as it's read, so this
// class has nothing further to "publish" — it exists to prove out the
// interface boundary that Phase 2's Gen2Telemetry will fill in with actual
// cloud publishing, without requiring any change to the callers.
// -----------------------------------------------------------------------------

#include "TelemetryProvider.h"

class LocalTelemetry : public TelemetryProvider {
public:
    bool publishEnvironment(const EnvironmentReading &reading, const char *sensorType) override;
    bool publishNetwork(const NetworkProbeResult results[], size_t count) override;
    bool publishDeviceStatus(const DeviceStatus &status) override;
};

#pragma once
// -----------------------------------------------------------------------------
// TelemetryProvider.h
//
// Future telemetry seam (section 31). Phase 1 ships only LocalTelemetry — a
// thin, mostly-no-op implementation, because EnvironmentManager/NetworkProbe
// already own local persistence directly. The interface exists so Phase 2
// can add Gen2Telemetry (device registration, cloud publish, remote config)
// without touching the environment/network/storage/dashboard layers.
//
// Do NOT implement Gen2Telemetry in Phase 1. Do NOT call any GEN2 endpoint
// from this file or its implementations.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "environment/EnvironmentManager.h" // EnvironmentReading
#include "network/NetworkProbe.h"           // NetworkProbeResult
#include "device/DeviceManager.h"           // DeviceStatus

class TelemetryProvider {
public:
    virtual ~TelemetryProvider() = default;

    virtual bool publishEnvironment(const EnvironmentReading &reading, const char *sensorType) = 0;
    virtual bool publishNetwork(const NetworkProbeResult results[], size_t count) = 0;
    virtual bool publishDeviceStatus(const DeviceStatus &status) = 0;
};

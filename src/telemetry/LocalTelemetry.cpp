#include "LocalTelemetry.h"

bool LocalTelemetry::publishEnvironment(const EnvironmentReading &reading, const char *sensorType) {
    (void)reading;
    (void)sensorType;
    // No-op: EnvironmentManager already wrote this reading to its bounded
    // history log. Nothing further to do locally.
    return true;
}

bool LocalTelemetry::publishNetwork(const NetworkProbeResult results[], size_t count) {
    (void)results;
    (void)count;
    // No-op: NetworkProbe already holds the latest result per target, which
    // the REST API and dashboard read directly.
    return true;
}

bool LocalTelemetry::publishDeviceStatus(const DeviceStatus &status) {
    (void)status;
    return true;
}

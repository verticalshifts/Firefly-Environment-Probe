#pragma once
// -----------------------------------------------------------------------------
// EnvironmentSensor.h
//
// Abstraction the rest of the app reads through, so DHT11 vs DHT22 is a
// runtime choice (section 7) that never leaks past this interface.
// -----------------------------------------------------------------------------

#include <Arduino.h>

class EnvironmentSensor {
public:
    virtual ~EnvironmentSensor() = default;

    virtual bool begin() = 0;

    // Attempts one reading. Returns false (and leaves temperature/humidity
    // untouched) on a failed/implausible read so callers never overwrite a
    // last-known-good value with garbage (section 9).
    virtual bool read(float &temperature, float &humidity) = 0;

    virtual const char *getSensorType() const = 0;
};

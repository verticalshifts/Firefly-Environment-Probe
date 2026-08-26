#pragma once
// -----------------------------------------------------------------------------
// DHTSensor.h
//
// Concrete EnvironmentSensor backed by the Adafruit DHT sensor library, which
// already supports DHT11/DHT21/DHT22 through one class parameterized by type
// — we just expose that as a runtime choice per section 7.
// -----------------------------------------------------------------------------

#include <DHT.h>
#include "EnvironmentSensor.h"

class DHTSensor : public EnvironmentSensor {
public:
    // sensorType: "DHT11" or "DHT22"
    DHTSensor(uint8_t gpio, const String &sensorType);

    bool begin() override;
    bool read(float &temperature, float &humidity) override;
    const char *getSensorType() const override { return sensorTypeName_; }

private:
    DHT dht_;
    uint8_t gpio_;
    const char *sensorTypeName_;

    // DHT sensors need >=2s (DHT11) / >=2s (DHT22) between reads; the library
    // itself rate-limits, but we track our own timestamp so a failed read
    // doesn't get retried faster than the sensor can physically respond.
    unsigned long lastReadAttemptMs_ = 0;
    static constexpr unsigned long MIN_READ_INTERVAL_MS = 2000;
};

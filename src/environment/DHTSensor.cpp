#include "DHTSensor.h"
#include "util/Logger.h"

static const char *TAG = "DHTSensor";

static uint8_t dhtTypeFromName(const String &sensorType, const char *&nameOut) {
    if (sensorType == "DHT11") {
        nameOut = "DHT11";
        return DHT11;
    }
    nameOut = "DHT22";
    return DHT22;
}

DHTSensor::DHTSensor(uint8_t gpio, const String &sensorType)
    : dht_(gpio, dhtTypeFromName(sensorType, sensorTypeName_)), gpio_(gpio) {}

bool DHTSensor::begin() {
    dht_.begin();
    Logger::info(TAG, String("Initialized ") + sensorTypeName_ + " on GPIO " + String(gpio_));
    return true;
}

bool DHTSensor::read(float &temperature, float &humidity) {
    unsigned long now = millis();
    if (now - lastReadAttemptMs_ < MIN_READ_INTERVAL_MS && lastReadAttemptMs_ != 0) {
        return false; // too soon; caller keeps last-known-good value
    }
    lastReadAttemptMs_ = now;

    float t = dht_.readTemperature();
    float h = dht_.readHumidity();

    if (isnan(t) || isnan(h)) {
        Logger::warn(TAG, "Sensor read returned NaN");
        return false;
    }

    // Plausibility bounds — reject values a DHT11/DHT22 cannot physically
    // produce, which is a cheap way to catch line noise / floating input.
    if (t < -40.0f || t > 80.0f || h < 0.0f || h > 100.0f) {
        Logger::warn(TAG, "Sensor read out of plausible range, discarding");
        return false;
    }

    temperature = t;
    humidity = h;
    return true;
}

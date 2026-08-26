#pragma once
// -----------------------------------------------------------------------------
// EnvironmentManager.h
//
// Owns the sensor, schedules non-blocking periodic reads, tracks the current
// reading + status, and appends bounded history (section 20/21/32).
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <memory>
#include "EnvironmentSensor.h"
#include "config/ConfigManager.h"
#include "util/CircularLog.h"

// Normalized internal data model (section 32), kept independent of the DHT
// library or any transport so a future Gen2Telemetry adapter can consume it
// as-is.
struct EnvironmentReading {
    float temperature = 0.0f;
    float humidity = 0.0f;
    bool valid = false;
    uint32_t timestamp = 0; // seconds since boot (millis()/1000)
};

enum class EnvironmentStatus { OK, SENSOR_ERROR, NOT_YET_READ };

struct EnvHistoryPoint {
    uint32_t timestamp;
    float temperature;
    float humidity;
} __attribute__((packed));

class EnvironmentManager {
public:
    explicit EnvironmentManager(ConfigManager &config);

    bool begin();

    // Call every loop() iteration; internally rate-limited by millis().
    void loop();

    const EnvironmentReading &current() const { return current_; }
    EnvironmentStatus status() const { return status_; }
    const char *sensorType() const { return sensor_ ? sensor_->getSensorType() : "NONE"; }

    // Reads up to `maxOut` most recent history points, oldest-first.
    uint32_t readHistory(EnvHistoryPoint *out, uint32_t maxOut);

    // Downsampled read of the most recent `rangeSeconds` of history, capped
    // to `maxOut` output points. See CircularLog::readRecentDownsampled.
    uint32_t readHistoryRange(uint32_t rangeSeconds, EnvHistoryPoint *out, uint32_t maxOut);

    // Re-creates the sensor object after a config change (sensor type/GPIO).
    void reconfigure();

    bool clearHistory() { return history_.clear(); }

private:
    ConfigManager &config_;
    std::unique_ptr<EnvironmentSensor> sensor_;
    CircularLog history_;

    EnvironmentReading current_;
    EnvironmentStatus status_ = EnvironmentStatus::NOT_YET_READ;
    uint8_t consecutiveFailures_ = 0;
    static constexpr uint8_t FAILURES_BEFORE_ERROR = 3;

    unsigned long lastReadMs_ = 0;
    unsigned long lastHistoryMs_ = 0;

    void createSensor();
};

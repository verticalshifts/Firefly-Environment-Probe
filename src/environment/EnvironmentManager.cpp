#include "EnvironmentManager.h"
#include "DHTSensor.h"
#include "hardware/HardwareConfig.h"
#include "util/Logger.h"

static const char *TAG = "Environment";

EnvironmentManager::EnvironmentManager(ConfigManager &config)
    : config_(config),
      history_("/history/env.bin", sizeof(EnvHistoryPoint), hw::HISTORY_MAX_RECORDS) {}

void EnvironmentManager::createSensor() {
    const DeviceConfig &c = config_.get();
    uint8_t gpio = c.sensorGpio != 0 ? c.sensorGpio : hw::DEFAULT_DHT_GPIO;
    sensor_.reset(new DHTSensor(gpio, c.sensorType));
    sensor_->begin();
    status_ = EnvironmentStatus::NOT_YET_READ;
    consecutiveFailures_ = 0;
}

bool EnvironmentManager::begin() {
    if (!history_.begin()) {
        Logger::error(TAG, "Failed to initialize history log");
    }
    createSensor();
    return true;
}

void EnvironmentManager::reconfigure() {
    Logger::info(TAG, "Reconfiguring sensor");
    createSensor();
}

void EnvironmentManager::loop() {
    unsigned long now = millis();
    uint32_t intervalMs = config_.get().environmentIntervalS * 1000UL;

    if (now - lastReadMs_ < intervalMs && lastReadMs_ != 0) return;
    lastReadMs_ = now;

    if (!sensor_) return;

    float t, h;
    bool ok = sensor_->read(t, h);

    if (ok) {
        current_.temperature = t;
        current_.humidity = h;
        current_.valid = true;
        current_.timestamp = now / 1000;
        consecutiveFailures_ = 0;
        status_ = EnvironmentStatus::OK;
    } else {
        consecutiveFailures_++;
        if (consecutiveFailures_ >= FAILURES_BEFORE_ERROR) {
            status_ = EnvironmentStatus::SENSOR_ERROR;
            // Deliberately leave `current_` untouched — a stale-but-valid
            // last reading is more useful on the dashboard than blanking it,
            // and status_ already communicates the error (section 9).
        }
    }

    // History is sampled at a fixed, coarser interval independent of the
    // live read cadence (section 21) to keep the flash footprint bounded.
    if (status_ == EnvironmentStatus::OK &&
        (now - lastHistoryMs_ >= hw::HISTORY_SAMPLE_INTERVAL_S * 1000UL || lastHistoryMs_ == 0)) {
        lastHistoryMs_ = now;
        EnvHistoryPoint point{current_.timestamp, current_.temperature, current_.humidity};
        history_.append(&point);
    }
}

uint32_t EnvironmentManager::readHistory(EnvHistoryPoint *out, uint32_t maxOut) {
    return history_.readRecent(out, maxOut);
}

uint32_t EnvironmentManager::readHistoryRange(uint32_t rangeSeconds, EnvHistoryPoint *out, uint32_t maxOut) {
    uint32_t desiredRecent = rangeSeconds / hw::HISTORY_SAMPLE_INTERVAL_S;
    if (desiredRecent == 0) desiredRecent = 1;
    return history_.readRecentDownsampled(desiredRecent, maxOut, out);
}

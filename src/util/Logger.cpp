#include "Logger.h"

String Logger::ring_[Logger::RING_SIZE];
size_t Logger::ringHead_ = 0;
size_t Logger::ringCount_ = 0;

static const char *levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::LOG_INFO:  return "INFO";
        case LogLevel::LOG_WARN:  return "WARN";
        case LogLevel::LOG_ERROR: return "ERROR";
    }
    return "INFO";
}

void Logger::log(LogLevel level, const String &tag, const String &msg) {
    String line = "[" + String(levelTag(level)) + "] " + tag + ": " + msg;
    Serial.println(line);

    ring_[ringHead_] = line;
    ringHead_ = (ringHead_ + 1) % RING_SIZE;
    if (ringCount_ < RING_SIZE) ringCount_++;
}

void Logger::recent(String out[RING_SIZE], size_t &count) {
    count = ringCount_;
    size_t start = (ringHead_ + RING_SIZE - ringCount_) % RING_SIZE;
    for (size_t i = 0; i < ringCount_; i++) {
        out[i] = ring_[(start + i) % RING_SIZE];
    }
}

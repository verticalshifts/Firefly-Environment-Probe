#pragma once
// -----------------------------------------------------------------------------
// Logger.h
//
// Lightweight diagnostic logging (section 37). Writes to Serial only — no
// flash-backed log file, to respect the memory/flash budget on ESP8266.
// Also keeps a small bounded in-RAM ring of the most recent lines so the
// diagnostics page can show "recent activity" without re-reading Serial.
// -----------------------------------------------------------------------------

#include <Arduino.h>

enum class LogLevel : uint8_t { LOG_INFO = 0, LOG_WARN = 1, LOG_ERROR = 2 };

class Logger {
public:
    static constexpr size_t RING_SIZE = 20;

    static void info(const String &tag, const String &msg) { log(LogLevel::LOG_INFO, tag, msg); }
    static void warn(const String &tag, const String &msg) { log(LogLevel::LOG_WARN, tag, msg); }
    static void error(const String &tag, const String &msg) { log(LogLevel::LOG_ERROR, tag, msg); }

    static void log(LogLevel level, const String &tag, const String &msg);

    // Returns up to RING_SIZE most recent log lines, oldest first.
    static void recent(String out[RING_SIZE], size_t &count);

private:
    static String ring_[RING_SIZE];
    static size_t ringHead_;
    static size_t ringCount_;
};

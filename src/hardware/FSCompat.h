#pragma once
// -----------------------------------------------------------------------------
// FSCompat.h
//
// Normalizes the small differences between the ESP32 and ESP8266 LittleFS
// APIs so the rest of the codebase can just call fscompat::begin()/fs().
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <LittleFS.h>

namespace fscompat {

inline bool begin() {
#if defined(PLATFORM_ESP32)
    return LittleFS.begin(true); // format on mount failure
#elif defined(PLATFORM_ESP8266)
    return LittleFS.begin(); // ESP8266 core auto-formats on mount failure
#endif
}

} // namespace fscompat

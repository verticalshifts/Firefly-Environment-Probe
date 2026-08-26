#include "PingCompat.h"

#if defined(PLATFORM_ESP32)
#include <ESP32Ping.h>
#elif defined(PLATFORM_ESP8266)
#include <ESP8266Ping.h>
#endif

bool pingcompat::ping(IPAddress ip, uint8_t count, float &avgMs, float &lossPct) {
    if (count == 0) count = 1;

    uint8_t successes = 0;
    float totalMs = 0.0f;

    for (uint8_t i = 0; i < count; i++) {
        if (Ping.ping(ip, 1)) {
            successes++;
            totalMs += (float)Ping.averageTime();
        }
        yield(); // keep watchdog/scheduler happy between packets
    }

    lossPct = 100.0f * (float)(count - successes) / (float)count;
    avgMs = successes > 0 ? totalMs / (float)successes : 0.0f;
    return successes > 0;
}

#pragma once
// -----------------------------------------------------------------------------
// PingCompat.h
//
// Normalizes ICMP ping across platforms: ESP32Ping (registry library) on
// ESP32, the framework-bundled ESP8266Ping on ESP8266. Both expose a very
// similar `Ping.ping()` / `Ping.averageTime()` surface, but rather than lean
// on library-specific multi-packet semantics that differ across versions, we
// drive our own single-packet-at-a-time loop here so the loss/latency
// statistics are computed the same way on both platforms.
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include <IPAddress.h>

namespace pingcompat {

// Sends `count` individual echo requests to `ip`. Returns true if at least
// one reply was received. avgMs is the average round-trip time of the
// successful replies (0 if none succeeded); lossPct is 0-100.
bool ping(IPAddress ip, uint8_t count, float &avgMs, float &lossPct);

} // namespace pingcompat

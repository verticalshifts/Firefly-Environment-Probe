#pragma once
// -----------------------------------------------------------------------------
// MonitorDef.h
//
// One network monitor, defined either by the user (Settings) or dispatched by
// GEN2 (see src/telemetry/Gen2MonitorSync.h). Replaces the fixed
// ProbeId { GATEWAY, PING_1, PING_2, DNS, HTTP } set — monitors are now a
// variable-length list, capped at hw::MAX_MONITORS.
//
// Definitions live in DeviceConfig (persisted to config.json); live results
// live in NetworkProbe and are matched back to a definition by `id`, NEVER by
// array index — removing a monitor from the middle of the list must not shift
// some other monitor's results onto it.
//
// `gen2Owned` marks a monitor that arrived via GEN2 dispatch. Those are
// read-only on the device: ConfigManager::update() preserves them no matter
// what a client submits, so the rule holds even if the UI is bypassed.
// -----------------------------------------------------------------------------

#include <Arduino.h>

enum class MonitorType : uint8_t { PING = 0, DNS, HTTP, PORT };

struct MonitorDef {
    String id;                          // 8 hex chars, stable for the monitor's lifetime
    String name;                        // display name; also the monitor name GEN2 knows
    MonitorType type = MonitorType::PING;
    String target;                      // IP (ping) | domain (dns) | URL (http) | host (port)
    uint16_t port = 0;                  // PORT only
    // Per-monitor "slow above this" bar, in ms. 0 = inherit (see
    // resolveLatencyHighMs below). Exists because one global threshold can't
    // serve both a LAN gateway and an HTTPS endpoint: the latter pays for a
    // TLS handshake and can never meet a 100ms bar, however healthy it is.
    uint16_t latencyHighMs = 0;
    bool gen2Owned = false;             // dispatched by GEN2 => read-only locally
};

// Per-type fallback bar, used when a monitor sets no explicit threshold.
// Connection-oriented checks include setup cost that ICMP simply doesn't, so
// holding them to the same number reports healthy endpoints as degraded.
// 0 means "no type-specific opinion — use the global latencyHighMs", which
// keeps ping and DNS behaving exactly as they did before per-monitor
// thresholds existed.
inline uint16_t monitorTypeDefaultLatencyMs(MonitorType t) {
    switch (t) {
        case MonitorType::HTTP: return 2000; // TLS handshake dominates the measurement
        case MonitorType::PORT: return 1000; // TCP connect, often across the internet
        default:                return 0;    // PING / DNS: the global threshold is apt
    }
}

// Explicit per-monitor override wins, then the per-type default, then the
// global threshold from config.
inline float resolveLatencyHighMs(const MonitorDef &m, float globalHighMs) {
    if (m.latencyHighMs > 0) return (float)m.latencyHighMs;
    uint16_t typeDefault = monitorTypeDefaultLatencyMs(m.type);
    return typeDefault > 0 ? (float)typeDefault : globalHighMs;
}

// The JSON/GEN2 spelling of each type. GEN2's job rows use exactly these
// strings for ping/http/port; it has no DNS type of its own, so "dns" only
// ever originates locally.
inline const char *monitorTypeName(MonitorType t) {
    switch (t) {
        case MonitorType::DNS:  return "dns";
        case MonitorType::HTTP: return "http";
        case MonitorType::PORT: return "port";
        default:                return "ping";
    }
}

// Returns false for an unrecognised name rather than defaulting to PING, so a
// caller can reject (or ack-and-skip) a monitor type this firmware doesn't
// implement instead of silently probing the wrong thing.
inline bool monitorTypeFromName(const String &s, MonitorType &out) {
    if (s == "ping") { out = MonitorType::PING; return true; }
    if (s == "dns")  { out = MonitorType::DNS;  return true; }
    if (s == "http") { out = MonitorType::HTTP; return true; }
    if (s == "port") { out = MonitorType::PORT; return true; }
    return false;
}

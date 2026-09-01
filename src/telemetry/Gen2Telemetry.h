#pragma once
// -----------------------------------------------------------------------------
// Gen2Telemetry.h
//
// TelemetryProvider implementation that POSTs environment (temperature/
// humidity) readings to GEN2 Bullseye's groundprobe endpoint at
// `<gen2ServerUrl>/api/groundprobe` — the host is user-configurable
// (`gen2ServerUrl`, default https://gen2bullseye.com), the `/api/groundprobe`
// path is fixed (the one path confirmed to resolve on every GEN2 host seen
// so far). Opt-in via config (`gen2Enabled`, default false) — a device with
// default config still makes zero GEN2 calls, so Phase 1's original "no
// GEN2 dependency" property holds for anyone who doesn't turn this on. See
// docs/architecture.md's "Phase 2 boundary" section for the full story.
//
// TLS uses setInsecure() — no certificate verification — despite this POST
// carrying a secret license key. This was a deliberate change, not an
// oversight: pinning GEN2's root CA (tried first) meant verifying an
// RSA-4096 chain, which took ~12.4s of CPU-bound signature-verification
// time on this chip and blocked loop() long enough to cause measurable WiFi
// packet loss (confirmed live: ICMP ping timeouts lined up exactly with
// that window). The tradeoff accepted here: a MITM on the network path
// could intercept gen2LicenseKey undetected. Reconsider if GEN2 ever
// exposes a faster (e.g. ECDSA-chain) host, or if this firmware moves to
// ESP32 (which has hardware RSA/SHA acceleration and would likely not hit
// this — untested, no ESP32 board was available to confirm) — see
// Gen2Telemetry.cpp for the exact setInsecure() call site.
//
// `latency_ms` is measured as a separate, bare TCP connect to the
// configured host — not the secure POST's own duration — since that
// duration is dominated by on-device certificate-verification CPU time
// (seconds, for an RSA-4096 chain, on this CPU) rather than real network
// latency; sending the POST's own duration was showing up as false "high
// latency" alerts on GEN2. See measureNetworkLatency() in the .cpp.
//
// publishNetwork()/publishDeviceStatus() are deliberately no-ops — only
// environment telemetry (temperature/humidity, which GEN2's `/groundprobe`
// accepts as aliases for `temperature_c`/`humidity_pct`) is in scope here.
// -----------------------------------------------------------------------------

#include "TelemetryProvider.h"
#include "config/ConfigManager.h"
#include "network/NetworkManager.h"
#include "device/DeviceManager.h"

class Gen2Telemetry : public TelemetryProvider {
public:
    Gen2Telemetry(ConfigManager &config, NetworkManager &network, DeviceManager &device);

    bool publishEnvironment(const EnvironmentReading &reading, const char *sensorType) override;

    // Out of scope for now — see class comment.
    bool publishNetwork(const NetworkProbeResult results[], size_t count) override;
    bool publishDeviceStatus(const DeviceStatus &status) override;

private:
    ConfigManager &config_;
    NetworkManager &network_;
    DeviceManager &device_;

    unsigned long lastPostMs_ = 0;

    bool postEnvironment(const EnvironmentReading &reading);
};

#include "TemperatureIndicator.h"

namespace {
constexpr unsigned long SLOW_ON_MS = 1000;
constexpr unsigned long SLOW_OFF_MS = 10000;
constexpr unsigned long FAST_ON_MS = 2000;
constexpr unsigned long FAST_OFF_MS = 1000;

constexpr float COLD_THRESHOLD_C = 20.0f;  // < this: SLOW_BLINK
constexpr float HOT_THRESHOLD_C = 28.0f;   // >= this: STEADY_ON
                                            // in between: FAST_BLINK

// This LED is wired with no series current-limiting resistor, so it's
// running well above its rated current straight off 3.3V — that's why it
// looked so bright. On ESP32, PWM-ing the "on" state (via the LEDC hardware
// peripheral) dims the *average* current/brightness — it does NOT cap the
// *peak* current during each on-pulse (still whatever the LED + GPIO's
// drive strength settle at, unregulated), so it reduces heat/wear but
// doesn't eliminate the real fix, a series resistor (typically 220-1k ohm
// for a 3.3V-supplied indicator LED, exact value depends on the LED's
// forward voltage/rated current). Treat this as a stopgap, not a
// substitute.
//
// ESP8266 does NOT get this treatment — see setLed()'s comment for why.
//
// Perceived brightness isn't linear with PWM duty — the eye is far more
// sensitive at the low end, so 50% duty (127) looked barely different from
// full on; ~15% duty gets much closer to visually "half as bright".
constexpr uint8_t LED_BRIGHTNESS = 38; // ~15% duty, 0-255 scale, ESP32 only
} // namespace

TemperatureIndicator::TemperatureIndicator(uint8_t gpio) : gpio_(gpio) {}

void TemperatureIndicator::begin() {
    pinMode(gpio_, OUTPUT);
    setLed(false);
}

TempLedPattern TemperatureIndicator::patternFor(float temperatureC, bool validReading) {
    if (!validReading) return TempLedPattern::OFF;
    if (temperatureC < COLD_THRESHOLD_C) return TempLedPattern::SLOW_BLINK;
    if (temperatureC < HOT_THRESHOLD_C) return TempLedPattern::FAST_BLINK;
    return TempLedPattern::STEADY_ON;
}

void TemperatureIndicator::loop(float temperatureC, bool validReading) {
    TempLedPattern pattern = patternFor(temperatureC, validReading);

    if (pattern != currentPattern_) {
        // Restart the on/off cycle cleanly on every band change rather than
        // carrying over timing from whatever phase the old pattern was in —
        // avoids e.g. landing mid-way through a 10s "off" phase right after
        // switching into STEADY_ON's very first tick.
        currentPattern_ = pattern;
        phaseStartMs_ = millis();
    }

    applyPattern();
}

void TemperatureIndicator::applyPattern() {
    switch (currentPattern_) {
        case TempLedPattern::OFF:
            setLed(false);
            return;
        case TempLedPattern::STEADY_ON:
            setLed(true);
            return;
        case TempLedPattern::SLOW_BLINK: {
            unsigned long pos = (millis() - phaseStartMs_) % (SLOW_ON_MS + SLOW_OFF_MS);
            setLed(pos < SLOW_ON_MS);
            return;
        }
        case TempLedPattern::FAST_BLINK: {
            unsigned long pos = (millis() - phaseStartMs_) % (FAST_ON_MS + FAST_OFF_MS);
            setLed(pos < FAST_ON_MS);
            return;
        }
    }
}

void TemperatureIndicator::setLed(bool on) {
    // Standard external LED wiring assumed (anode -> GPIO, cathode -> GND).
    // If wired the other way around (LED to 3.3V, GPIO sinks it), flip the
    // HIGH/LOW (and, on ESP32, the brightness/255 subtraction) below.
#if defined(PLATFORM_ESP32)
    // ESP32's analogWrite() drives the LEDC peripheral — genuine hardware
    // PWM, independent of WiFi's own timing. Safe to dim here.
    analogWrite(gpio_, on ? LED_BRIGHTNESS : 0);
#elif defined(PLATFORM_ESP8266)
    // ESP8266 has no hardware PWM peripheral — analogWrite() here is a
    // software waveform generator driven by a timer interrupt
    // (core_esp8266_wiring_pwm.cpp's startWaveformClockCycles()), and that
    // interrupt competes with WiFi's own timing-critical interrupts on this
    // single-core chip. Confirmed live: switching this LED to analogWrite()
    // caused real ping packet loss to the device that went away once
    // reverted to plain digitalWrite. So: full brightness only here — no
    // firmware-side dimming is safe on this chip without risking WiFi
    // reliability, regardless of duty cycle or PWM frequency. The actual
    // fix for both the overcurrent risk and brightness is the same series
    // resistor mentioned above (picking a higher resistance both protects
    // the LED/GPIO and directly reduces DC current/brightness, with zero
    // PWM involved).
    digitalWrite(gpio_, on ? HIGH : LOW);
#endif
}

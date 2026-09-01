#pragma once
// -----------------------------------------------------------------------------
// TemperatureIndicator.h
//
// Drives a single GPIO LED (hw::DEFAULT_TEMP_LED_GPIO) to give an at-a-glance
// physical read of the current temperature band without needing the
// dashboard open:
//
//   < 20C          10s off, 1s on   (mostly off — cold)
//   20C .. <28C     2s on, 1s off   (mostly on — comfortable)
//   >= 28C          steady on       (hot)
//
// Non-blocking (millis()-gated), matching every other manager's loop()
// idiom in this codebase — no delay() calls. While the sensor hasn't
// produced a valid reading yet (boot, or SENSOR_ERROR), the LED is held off
// rather than showing a stale or misleading pattern.
//
// "On" is dimmed via PWM (analogWrite, LED_BRIGHTNESS/255 duty) on ESP32
// only — ESP32's analogWrite is real hardware PWM (LEDC), safe alongside
// WiFi. ESP8266 has no hardware PWM; its analogWrite is a software
// timer-interrupt waveform generator that was confirmed, live, to cause
// WiFi packet loss, so ESP8266 always drives this LED at full brightness
// via plain digitalWrite — see setLed() in the .cpp for the full story and
// why a series resistor (this LED currently has none) is the only safe way
// to dim it on that platform.
// -----------------------------------------------------------------------------

#include <Arduino.h>

enum class TempLedPattern { OFF, SLOW_BLINK, FAST_BLINK, STEADY_ON };

class TemperatureIndicator {
public:
    explicit TemperatureIndicator(uint8_t gpio);

    void begin();

    // Call every loop() iteration. temperatureC is only read when
    // validReading is true.
    void loop(float temperatureC, bool validReading);

private:
    uint8_t gpio_;
    TempLedPattern currentPattern_ = TempLedPattern::OFF;
    unsigned long phaseStartMs_ = 0; // start of the current pattern's on/off cycle

    static TempLedPattern patternFor(float temperatureC, bool validReading);
    void applyPattern();
    void setLed(bool on); // PWM-dimmed on ESP32, full brightness on ESP8266 — see .cpp
};

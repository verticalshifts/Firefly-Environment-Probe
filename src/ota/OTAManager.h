#pragma once
// -----------------------------------------------------------------------------
// OTAManager.h
//
// Thin wrapper around the Update library (identical header/API surface on
// both ESP32 and ESP8266 cores) so WebServerManager's upload handler doesn't
// touch Update.h directly (section 26).
// -----------------------------------------------------------------------------

#include <Arduino.h>

class OTAManager {
public:
    // sizeHint: total upload size if known (Content-Length), 0 if unknown.
    bool start(size_t sizeHint);
    bool write(uint8_t *data, size_t len);

    // Finalizes the update. Returns true only if the image was complete and
    // valid (Update.h performs its own header/size/CRC validation, which is
    // the "appropriate for the target platform" check from section 26 — we
    // don't duplicate that logic here).
    bool finish();

    String lastError() const { return lastError_; }

private:
    String lastError_;
};

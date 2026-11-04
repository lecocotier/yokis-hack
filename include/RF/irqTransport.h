#pragma once
#include <Arduino.h>

// Dedicated, non-virtual ISR transport. Foreground SPI has already established
// the mode/clock; the saved-state transaction guard prevents concurrent access.
class RadioIrqTransport {
    uint32_t ceMask_, csnMask_;
    uint16_t cePin_, csnPin_;
    uint8_t exchange(uint8_t command, const uint8_t* tx, uint8_t* rx, uint8_t length, bool* success = nullptr);
public:
    RadioIrqTransport(uint16_t ce, uint16_t csn);
    uint8_t statusAndClear();
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readPayload(uint8_t* output, uint8_t length);
    void ce(bool high);
};

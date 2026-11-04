/* HSPI transfer sequence adapted from ESP8266 Arduino core 3.1.2 SPI.cpp.
 * Copyright (c) 2015 Hristo Gochkov. LGPL-2.1-or-later.
 * See doc/LGPL-2.1.txt. The bounded wait and IRQ-only transport are local
 * adaptations; the register setup intentionally follows that pinned core.
 */
#include "RF/irqTransport.h"
#include <nRF24L01.h>
#if defined(YOKIS_RADIO_TEST)
#include "radioTestHardware.h"
#define IRQ_CODE
#elif defined(ESP8266)
#include <esp8266_peri.h>
extern "C" void ets_delay_us(uint32_t);
#define IRQ_CODE IRAM_ATTR
#else
#include <SPI.h>
#define IRQ_CODE
#endif

RadioIrqTransport::RadioIrqTransport(uint16_t ce, uint16_t csn)
    : ceMask_(uint32_t(1) << ce), csnMask_(uint32_t(1) << csn), cePin_(ce), csnPin_(csn) {}

#if defined(ESP8266) && !defined(YOKIS_RADIO_TEST)
// Same 8-bit HSPI register sequence as core SPIClass::transfer (3.1.2), without
// flash-resident methods. The bounded wait handles a wedged peripheral without
// trapping the CPU in an ISR. No SPI mode/clock changes are made here.
static IRQ_CODE bool irqTransfer(uint8_t byte, uint8_t& result) {
    unsigned budget = 1024;
    while (SPI1CMD & SPIBUSY) if (!--budget) return false;
    const uint32_t mask = ~((SPIMMOSI << SPILMOSI) | (SPIMMISO << SPILMISO));
    SPI1U1 = (SPI1U1 & mask) | (7 << SPILMOSI) | (7 << SPILMISO);
    SPI1W0 = byte;
    SPI1CMD |= SPIBUSY;
    budget = 1024;
    while (SPI1CMD & SPIBUSY) if (!--budget) return false;
    result = uint8_t(SPI1W0);
    return true;
}
#endif

IRQ_CODE uint8_t RadioIrqTransport::exchange(uint8_t command, const uint8_t* tx,
                                             uint8_t* rx, uint8_t length, bool* success) {
    uint8_t status = 0, byte = 0;
    bool ok = true;
#if defined(YOKIS_RADIO_TEST)
    radioTestCsn(csnPin_, false); radioTestDelay(5);
    ok = radioTestIrqTransfer(command, status);
    for (uint8_t i=0; ok && i<length; ++i) {
        ok = radioTestIrqTransfer(tx ? tx[i] : 0xff, byte);
        if (rx && ok) rx[i] = byte;
    }
    radioTestCsn(csnPin_, true); radioTestDelay(5);
#elif defined(ESP8266)
    GPOC = csnMask_; ets_delay_us(5);
    ok = irqTransfer(command, status);
    for (uint8_t i=0; ok && i<length; ++i) {
        ok = irqTransfer(tx ? tx[i] : 0xff, byte);
        if (rx && ok) rx[i] = byte;
    }
    GPOS = csnMask_; ets_delay_us(5);
#else
    digitalWrite(csnPin_, LOW); delayMicroseconds(5);
    status = SPI.transfer(command);
    for (uint8_t i=0; i<length; ++i) {
        byte = SPI.transfer(tx ? tx[i] : 0xff);
        if (rx) rx[i] = byte;
    }
    digitalWrite(csnPin_, HIGH); delayMicroseconds(5);
#endif
    if (success) *success = ok;
    if (!ok) status = 0; // Do not manufacture TX/RX flags after a bus timeout.
    return status;
}
IRQ_CODE uint8_t RadioIrqTransport::statusAndClear() {
    const uint8_t flags = 0x70;
    return exchange(W_REGISTER | NRF_STATUS, &flags, nullptr, 1);
}
IRQ_CODE bool RadioIrqTransport::writeRegister(uint8_t reg, uint8_t value) {
    bool ok;
    exchange(W_REGISTER | (REGISTER_MASK & reg), &value, nullptr, 1, &ok);
    return ok;
}
IRQ_CODE bool RadioIrqTransport::readPayload(uint8_t* output, uint8_t length) {
    if (length > 32) length = 32;
    bool ok;
    exchange(R_RX_PAYLOAD, nullptr, output, length, &ok);
    return ok;
}
IRQ_CODE void RadioIrqTransport::ce(bool high) {
#if defined(YOKIS_RADIO_TEST)
    radioTestCe(cePin_, high);
#elif defined(ESP8266)
    if (high) GPOS = ceMask_; else GPOC = ceMask_;
#else
    digitalWrite(cePin_, high);
#endif
}

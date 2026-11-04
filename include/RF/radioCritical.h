#pragma once
#include <stdint.h>
#if defined(YOKIS_RADIO_TEST)
#include "radioTestHardware.h"
#elif defined(YOKIS_NATIVE_TEST)
#include <Arduino.h>
#elif defined(ESP8266)
#include <core_esp8266_features.h>
#else
#include <avr/io.h>
#include <avr/interrupt.h>
#endif

namespace Yokis {
// Only a command/data/CSN transaction is protected. Radio waits and yields must
// stay outside this scope. Restore the caller's state, including a masked ISR.
class RadioCritical {
    uint32_t saved_;
    bool active_;
public:
    __attribute__((always_inline)) RadioCritical() : active_(true) {
#if defined(YOKIS_RADIO_TEST)
        saved_ = radioTestDisable();
#elif defined(YOKIS_NATIVE_TEST)
        saved_ = 0;
#elif defined(ESP8266)
        saved_ = xt_rsil(15);
#else
        saved_ = SREG;
        cli();
        __asm__ __volatile__("" ::: "memory");
#endif
    }
    __attribute__((always_inline)) ~RadioCritical() { release(); }
    __attribute__((always_inline)) void release() {
        if (!active_) return;
        active_ = false;
#if defined(YOKIS_RADIO_TEST)
        radioTestRestore(saved_);
#elif defined(YOKIS_NATIVE_TEST)
        (void)saved_;
#elif defined(ESP8266)
        xt_wsr_ps(saved_);
#else
        __asm__ __volatile__("" ::: "memory");
        SREG = uint8_t(saved_);
        __asm__ __volatile__("" ::: "memory");
#endif
    }
    RadioCritical(const RadioCritical&) = delete;
    RadioCritical& operator=(const RadioCritical&) = delete;
};
}

#pragma once
#include <stdint.h>
namespace Yokis {
// Double short flash: WiFi absent. Slow blink: configured MQTT absent.
// Healthy or deliberately unconfigured MQTT: LED off. No delay or timer owner.
inline bool statusLedOn(uint32_t now, bool wifi, bool mqtt, bool configured) {
    if (!wifi) return now % 1000 < 100 || (now % 1000 >= 200 && now % 1000 < 300);
    return configured && !mqtt && now % 2000 < 1000;
}
}

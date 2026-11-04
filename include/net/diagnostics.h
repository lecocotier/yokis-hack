#pragma once

#include <stddef.h>
#include <stdint.h>

class Device;

namespace Yokis {

struct DiagnosticsSnapshot {
    const char *buildId;
    const char *gitRevision;
    const char *sourceSha256;
    const char *ssid;
    const char *ip;
    const char *mqttHost;
    uint32_t uptimeMs;
    uint32_t freeHeap;
    int rssi;
    uint16_t mqttPort;
    uint8_t debugLevel;
    bool wifiConnected;
    bool mqttConnected;
    bool mqttConfigured;
};

// Serializes cached observations only; requesting diagnostics never starts RF work.
// Pages contain at most eight non-null devices, with at most 64 devices total.
// On failure, a nonempty destination is cleared so callers cannot serve partial JSON.
bool writeDiagnosticJson(char *dst, size_t capacity,
                         const DiagnosticsSnapshot &snapshot,
                         Device *const *devices, size_t count, size_t page);

} // namespace Yokis

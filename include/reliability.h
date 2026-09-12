#ifndef YOKIS_RELIABILITY_H
#define YOKIS_RELIABILITY_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Yokis {
static const size_t DeviceNameMax = 48;
static const size_t ConfigLineSize = 128;

// millis() is 32 bit on both supported targets. Unsigned subtraction also
// works across rollover; all application intervals are far below 2^31 ms.
inline bool elapsed(uint32_t now, uint32_t start, uint32_t interval) {
    return uint32_t(now - start) >= interval;
}

inline bool copyText(char* dst, size_t capacity, const char* src) {
    if (!dst || !capacity) return false;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= capacity) return false; // reject, never silently truncate settings
    memmove(dst, src, n + 1);
    return true;
}

inline bool unsignedNumber(const char* text, uint32_t maximum,
                           uint32_t& value, uint8_t base = 10) {
    if (!text || !*text || (base != 10 && base != 16)) return false;
    uint32_t result = 0;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        unsigned digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (base == 16 && *p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
        else if (base == 16 && *p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
        else return false;
        if (digit >= base || digit > maximum || result > (maximum - digit) / base)
            return false;
        result = result * base + digit;
    }
    value = result;
    return true;
}

inline bool validDeviceName(const char* text) {
    if (!text || !*text || strlen(text) > DeviceNameMax) return false;
    for (const unsigned char* p = (const unsigned char*)text; *p; ++p) {
        // Device names are also CLI arguments, MQTT topic levels and JSON IDs.
        if (*p <= 32 || *p == 127 || strchr("/#+|\\\"", *p)) return false;
    }
    return true;
}

inline bool configField(const char* s) {
    return s && !strchr(s, '|') && !strchr(s, '\r') && !strchr(s, '\n');
}

enum Command : uint8_t {
    InvalidCommand = 0, PowerOn, PowerOff, Stop,
    DimmerOff, DimmerMin, DimmerMid, DimmerMax
};

struct MqttRequest {
    char device[DeviceNameMax + 1];
    Command command;
    bool brightness;
};

inline bool parseMqtt(const char* topic, const uint8_t* payload, size_t size,
                      MqttRequest& request) {
    request.command = InvalidCommand;
    if (!topic || !payload || !size) return false;
    const char* slash = strchr(topic, '/');
    if (!slash) return false;
    size_t n = slash - topic;
    if (!n || n > DeviceNameMax) return false;
    memcpy(request.device, topic, n); request.device[n] = '\0';
    if (!validDeviceName(request.device)) return false;
    bool power = strcmp(slash, "/cmnd/POWER") == 0;
    request.brightness = strcmp(slash, "/cmnd/BRIGHTNESS") == 0;
    if (power) {
        if (size == 2 && memcmp(payload, "ON", 2) == 0) request.command = PowerOn;
        if (size == 3 && memcmp(payload, "OFF", 3) == 0) request.command = PowerOff;
        if (size == 5 && memcmp(payload, "PAUSE", 5) == 0) request.command = Stop;
    } else if (request.brightness && size == 1) {
        switch (payload[0]) {
            case '0': request.command = DimmerOff; break;
            case '1': request.command = DimmerMin; break;
            case '2': request.command = DimmerMid; break;
            case '3': case '4': request.command = DimmerMax; break;
        }
    }
    return request.command != InvalidCommand;
}

// The window applies ONLY to the same acknowledged command, never to polls.
// STOP is always admitted, including consecutive stops.
struct CommandHistory {
    Command last = InvalidCommand;
    uint32_t at = 0;
    bool duplicate(Command cmd, uint32_t now) const {
        return cmd != Stop && cmd == last && last != InvalidCommand &&
               !elapsed(now, at, 100);
    }
    void acknowledged(Command cmd, uint32_t now) { last = cmd; at = now; }
};
// Small bounded CLI tokenizer. Quotes preserve WiFi/MQTT arguments with
// spaces; malformed quotes and extra arguments are rejected, not truncated.
struct Arguments {
    char buffer[256];
    char* value[6];
    unsigned count;
    bool valid;
    explicit Arguments(const char* input) : count(0), valid(true) {
        buffer[0] = 0;
        if (!input) return;
        if (strlen(input) >= sizeof(buffer)) { valid = false; return; }
        char* out = buffer;
        while (*input) {
            while (*input == ' ' || *input == '\t') ++input;
            if (!*input) break;
            if (count == 6) { valid = false; return; }
            value[count++] = out;
            bool quoted = *input == '"';
            if (quoted) ++input;
            while (*input && (quoted ? *input != '"' : (*input != ' ' && *input != '\t')))
                *out++ = *input++;
            if (quoted) {
                if (*input != '"') { valid = false; return; }
                ++input;
                if (*input && *input != ' ' && *input != '\t') { valid = false; return; }
            }
            *out++ = 0;
        }
    }
    const char* at(unsigned i) const { return i < count ? value[i] : ""; }
};

}
#endif

#include "net/diagnostics.h"
#include "RF/device.h"

namespace Yokis {
namespace {

constexpr size_t pageSize = 8;
constexpr size_t maxDevices = 64;

// No allocation or network calls: a failed write invalidates the entire response,
// so a short HTTP buffer cannot accidentally expose a syntactically partial snapshot.
class JsonWriter {
public:
    JsonWriter(char *dst, size_t capacity)
        : dst_(dst), capacity_(capacity), used_(0), valid_(dst && capacity) {
        if (valid_) dst_[0] = '\0';
    }

    void character(char value) {
        if (!valid_) return;
        if (used_ >= capacity_ - 1) {
            valid_ = false;
            dst_[0] = '\0';
            return;
        }
        dst_[used_++] = value;
        dst_[used_] = '\0';
    }

    void literal(const char *value) {
        while (valid_ && *value) character(*value++);
    }

    void string(const char *value) {
        character('"');
        if (value) {
            while (valid_ && *value) {
                const unsigned char byte = static_cast<unsigned char>(*value++);
                switch (byte) {
                case '"': literal("\\\""); break;
                case '\\': literal("\\\\"); break;
                case '\b': literal("\\b"); break;
                case '\f': literal("\\f"); break;
                case '\n': literal("\\n"); break;
                case '\r': literal("\\r"); break;
                case '\t': literal("\\t"); break;
                default:
                    if (byte < 0x20) {
                        static const char hex[] = "0123456789abcdef";
                        literal("\\u00");
                        character(hex[byte >> 4]);
                        character(hex[byte & 0x0f]);
                    } else {
                        // Preserve UTF-8 bytes; only JSON delimiters and controls escape.
                        character(static_cast<char>(byte));
                    }
                }
            }
        }
        character('"');
    }

    void number(uint32_t value) {
        char digits[10];
        size_t length = 0;
        do {
            digits[length++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        while (length) character(digits[--length]);
    }

    void signedNumber(int value) {
        if (value < 0) {
            character('-');
            // Unsigned subtraction also handles the most-negative signed value.
            number(0U - static_cast<uint32_t>(value));
        } else {
            number(static_cast<uint32_t>(value));
        }
    }

    void boolean(bool value) { literal(value ? "true" : "false"); }
    bool valid() const { return valid_; }

private:
    char *dst_;
    size_t capacity_;
    size_t used_;
    bool valid_;
};

void writeDevice(JsonWriter &out, Device &device, uint32_t now) {
    const auto &feedback = device.shutterFeedback();
    out.literal("{\"name\":");
    out.string(device.getName());
    out.literal(",\"mode\":");
    out.string(Device::getModeAsString(device.getMode()));
    out.literal(",\"status\":");
    out.string(Device::getStatusAsString(device.getStatus()));
    out.literal(",\"availability\":");
    out.string(Device::getAvailabilityAsString(device.getAvailability()));
    out.literal(",\"status_age_ms\":");
    // The supplied snapshot timestamp keeps all ages coherent, including millis wrap.
    out.number(static_cast<uint32_t>(now - device.getLastUpdateMillis()));
    out.literal(",\"state_source\":");
    out.string(feedback.sourceName());
    out.literal(",\"estimated\":");
    out.boolean(feedback.estimated());
    out.literal(",\"last_command\":");
    out.string(feedback.commandName());
    out.literal(",\"stop_check\":");
    out.string(feedback.verification().name());
    out.character('}');
}

} // namespace

bool writeDiagnosticJson(char *dst, size_t capacity,
                         const DiagnosticsSnapshot &snapshot,
                         Device *const *devices, size_t count, size_t page) {
    if (dst && capacity) dst[0] = '\0';
    if (!dst || !capacity || (!devices && count)) return false;

    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (devices[i] && ++total > maxDevices) return false;
    }
    // Validate before multiplication so even SIZE_MAX cannot wrap into a valid page.
    if ((total == 0 && page != 0) || (total && page > (total - 1) / pageSize)) return false;
    const size_t first = page * pageSize;
    const size_t end = first + pageSize;

    JsonWriter out(dst, capacity);
    out.literal("{\"build\":");
    out.string(snapshot.buildId);
    out.literal(",\"git\":");
    out.string(snapshot.gitRevision);
    out.literal(",\"source_sha256\":");
    out.string(snapshot.sourceSha256);
    out.literal(",\"debug_level\":");
    out.number(snapshot.debugLevel);
    out.literal(",\"uptime_ms\":");
    out.number(snapshot.uptimeMs);
    out.literal(",\"free_heap\":");
    out.number(snapshot.freeHeap);
    out.literal(",\"wifi\":{\"connected\":");
    out.boolean(snapshot.wifiConnected);
    out.literal(",\"ssid\":");
    out.string(snapshot.ssid);
    out.literal(",\"ip\":");
    out.string(snapshot.ip);
    out.literal(",\"rssi\":");
    out.signedNumber(snapshot.rssi);
    out.literal("},\"mqtt\":{\"connected\":");
    out.boolean(snapshot.mqttConnected);
    out.literal(",\"configured\":");
    out.boolean(snapshot.mqttConfigured);
    out.literal(",\"host\":");
    out.string(snapshot.mqttHost);
    out.literal(",\"port\":");
    out.number(snapshot.mqttPort);
    out.literal("},\"page\":");
    out.number(static_cast<uint32_t>(page));
    out.literal(",\"page_size\":");
    out.number(static_cast<uint32_t>(pageSize));
    out.literal(",\"total\":");
    out.number(static_cast<uint32_t>(total));
    out.literal(",\"has_next\":");
    out.boolean(end < total);
    out.literal(",\"devices\":[");
    size_t ordinal = 0;
    bool needsComma = false;
    for (size_t i = 0; i < count && ordinal < end; ++i) {
        if (!devices[i]) continue;
        if (ordinal++ < first) continue;
        if (needsComma) out.character(',');
        writeDevice(out, *devices[i], snapshot.uptimeMs);
        needsComma = true;
    }
    out.literal("]}");
    return out.valid();
}

} // namespace Yokis

#include "net/discoveryCleanup.h"

#if defined(ESP8266) && defined(MQTT_ENABLED)
#include <LittleFS.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include "net/mqttHass.h"
#include "reliability.h"
#include "storage/yokisLittleFS.h"

namespace {
const char JournalPath[] = "/discovery.cleanup";
const char TemporaryPath[] = "/discovery.cleanup.tmp";
const char JournalHeader[] = "YKDISC1";
const size_t MaximumRecords = 128;
const size_t MaximumNameLength = 48;

struct Record {
    char name[MaximumNameLength + 1];
    char component;
    uint8_t stage;
};

enum class LineResult { Complete, End, Invalid };

bool validName(const char* name) {
    if (!name) return false;
    size_t length = 0;
    while (length <= MaximumNameLength && name[length]) {
        if (name[length] == '|' || name[length] == '\r' || name[length] == '\n') return false;
        ++length;
    }
    return length != 0 && length <= MaximumNameLength && Yokis::validDeviceName(name);
}

LineResult readLine(File& source, char* text, size_t capacity) {
    size_t length = 0;
    while (source.available()) {
        const int byte = source.read();
        if (byte < 0) return LineResult::Invalid;
        if (byte == '\n') {
            if (length && text[length - 1] == '\r') --length;
            text[length] = '\0';
            return LineResult::Complete;
        }
        if (byte == 0 || (byte < 32 && byte != '\r') || length + 1 >= capacity)
            return LineResult::Invalid;
        text[length++] = static_cast<char>(byte);
    }
    // A truncated final record is not a committed intent to delete anything.
    return length == 0 ? LineResult::End : LineResult::Invalid;
}

bool parseRecord(char* line, Record& record) {
    const size_t length = std::strlen(line);
    if (length < 5 || line[length - 4] != '|' || line[length - 2] != '|') return false;
    const char component = line[length - 3];
    const char stage = line[length - 1];
    if ((component != 'l' && component != 'c') || stage < '0' || stage > '4') return false;
    line[length - 4] = '\0';
    if (!validName(line)) return false;
    std::memcpy(record.name, line, length - 4 + 1);
    record.component = component;
    record.stage = static_cast<uint8_t>(stage - '0');
    return true;
}

bool loadRecords(Record* records, size_t& count) {
    count = 0;
    if (!LittleFS.exists(JournalPath)) return true;
    File source = LittleFS.open(JournalPath, "r");
    if (!source) return false;
    char line[60];
    bool valid = readLine(source, line, sizeof(line)) == LineResult::Complete &&
                 std::strcmp(line, JournalHeader) == 0;
    while (valid) {
        const LineResult result = readLine(source, line, sizeof(line));
        if (result == LineResult::End) break;
        if (result != LineResult::Complete || count == MaximumRecords ||
            !parseRecord(line, records[count])) {
            valid = false;
            break;
        }
        for (size_t i = 0; i < count; ++i) {
            if (records[i].component == records[count].component &&
                std::strcmp(records[i].name, records[count].name) == 0) {
                valid = false;
                break;
            }
        }
        if (valid) ++count;
    }
    source.close();
    return valid;
}

bool writeText(File& destination, const char* text) {
    for (; *text; ++text) {
        if (destination.write(static_cast<uint8_t>(*text)) != 1) return false;
    }
    return true;
}

bool saveRecords(const Record* records, size_t count) {
    if (count == 0)
        return !LittleFS.exists(JournalPath) || LittleFS.remove(JournalPath);

    File destination = LittleFS.open(TemporaryPath, "w");
    if (!destination) return false;
    bool written = writeText(destination, JournalHeader) && destination.write('\n') == 1;
    for (size_t i = 0; written && i < count; ++i) {
        written = writeText(destination, records[i].name) &&
                  destination.write('|') == 1 &&
                  destination.write(static_cast<uint8_t>(records[i].component)) == 1 &&
                  destination.write('|') == 1 &&
                  destination.write(static_cast<uint8_t>('0' + records[i].stage)) == 1 &&
                  destination.write('\n') == 1;
    }
    destination.flush();
    written = written && destination.getWriteError() == 0;
    destination.close();
    if (!written || !LittleFS.rename(TemporaryPath, JournalPath)) {
        LittleFS.remove(TemporaryPath);
        return false;
    }
    return true;
}

void removeFirst(Record* records, size_t& count) {
    for (size_t i = 1; i < count; ++i) records[i - 1] = records[i];
    --count;
}

bool coverDevice(const Device& device) {
    const auto mode = device.getMode();
    if (mode == SHUTTER) return true;
    const char* label = Device::getModeAsString(mode);
    // Recognize the future public mode without assigning it a numeric value.
    return label && std::strcmp(label, "SHUTTER_BUS") == 0;
}

bool makeTopic(const Record& record, char* topic, size_t capacity) {
    int length;
    if (record.stage == 0) {
        length = std::snprintf(topic, capacity, "%s/%s/%s/config", HASS_PREFIX,
                               record.component == 'c' ? "cover" : "light", record.name);
    } else {
        const char* const suffix[] = {"", "STATE", "DETAIL", "BRIGHTNESS", "LWT"};
        length = std::snprintf(topic, capacity, "%s/tele/%s", record.name, suffix[record.stage]);
    }
    return length >= 0 && static_cast<size_t>(length) < capacity;
}
}

namespace Yokis {
bool queueDiscoveryCleanup(const char* name, bool cover) {
    if (!validName(name) || !YokisLittleFS::init()) return false;
    std::unique_ptr<Record[]> records(new (std::nothrow) Record[MaximumRecords]);
    if (!records) return false;
    size_t count = 0;
    if (!loadRecords(records.get(), count)) return false;
    const char component = cover ? 'c' : 'l';
    for (size_t i = 0; i < count; ++i) {
        if (records[i].component == component && std::strcmp(records[i].name, name) == 0)
            return true;
    }
    if (count == MaximumRecords) return false;
    Record& added = records[count++];
    std::strcpy(added.name, name);
    added.component = component;
    added.stage = 0;
    return saveRecords(records.get(), count);
}

bool hasPendingDiscoveryCleanup() {
    return YokisLittleFS::init() && LittleFS.exists(JournalPath);
}

CleanupResult serviceDiscoveryCleanup(PubSubClient& client,
                                      Device* const* active, size_t slots) {
    if (!YokisLittleFS::init()) return CleanupResult::Blocked;
    if (!LittleFS.exists(JournalPath)) return CleanupResult::Idle;
    if (!client.connected()) return CleanupResult::Blocked;
    std::unique_ptr<Record[]> records(new (std::nothrow) Record[MaximumRecords]);
    if (!records) return CleanupResult::Blocked;
    size_t count = 0;
    if (!loadRecords(records.get(), count)) return CleanupResult::Blocked;
    if (count == 0)
        return saveRecords(records.get(), 0) ? CleanupResult::Progress : CleanupResult::Blocked;

    Record& first = records[0];
    bool sameComponent = false;
    bool differentComponent = false;
    if (active) {
        for (size_t i = 0; i < slots; ++i) {
            if (!active[i] || std::strcmp(active[i]->getName(), first.name) != 0) continue;
            if (coverDevice(*active[i]) == (first.component == 'c')) sameComponent = true;
            else differentComponent = true;
        }
    }
    // Reconcile on every call: failed config transactions and recreated devices
    // must not lose their current discovery or shared retained telemetry.
    if (sameComponent || (differentComponent && first.stage != 0)) {
        removeFirst(records.get(), count);
        return saveRecords(records.get(), count) ? CleanupResult::Progress : CleanupResult::Blocked;
    }

    char topic[192];
    if (!makeTopic(first, topic, sizeof(topic)) || !client.publish(topic, "", true))
        return CleanupResult::Blocked;
    // QoS 0 acceptance only: a crash or failed checkpoint may replay this empty
    // retained publication. Repetition is safe; broker persistence is not proven.
    if (differentComponent || first.stage == 4) removeFirst(records.get(), count);
    else ++first.stage;
    return saveRecords(records.get(), count) ? CleanupResult::Progress : CleanupResult::Blocked;
}
}
#endif

#include "RF/device.h"

#include "globals.h"
#include "utils.h"

#if defined(ESP8266)
#include <FS.h>
#endif

Device::Device(const char* dname)
    : channel(0), mode(ON_OFF), status(UNDEFINED), availability(ONLINE),
      brightness(BRIGHTNESS_OFF), lastUpdateMillis(0),
      hasToBePolledForStatus(false), failedPolls(0) {
    name[0] = '\0';
    memset(hardwareAddress, 0, sizeof(hardwareAddress));
    memset(serial, 0, sizeof(serial));
    memset(version, 0, sizeof(version));
    addressConfigured = channelConfigured = false;
    setName(dname);
}
Device::Device(const Device* d) : Device(d ? d->getName() : "") { copy(d); }
Device::Device(const char* n, const uint8_t* a, uint8_t c) : Device(n) {
    setHardwareAddress(a); setChannel(c);
}
Device::Device(const char* n, const uint8_t* a, uint8_t c,
               const uint8_t* serial, const uint8_t* version) : Device(n, a, c) {
    setSerial(serial); setVersion(version);
}
Device::~Device() {}

const char* Device::getName() const { return this->name; }

const uint8_t* Device::getHardwareAddress() const {
    return addressConfigured ? this->hardwareAddress : NULL;
}

uint8_t Device::getChannel() const { return this->channel; }

const uint8_t* Device::getVersion() const { return this->version; }

const uint8_t* Device::getSerial() const { return this->serial; }

const DeviceMode Device::getMode() const { return mode; }

const DeviceStatus Device::getStatus() const { return status; }

const DimmerBrightness Device::getBrightness() const { return brightness; }

const DeviceAvailability Device::getAvailability() const {
    return availability;
}

const unsigned long Device::getLastUpdateMillis() const {
    return lastUpdateMillis;
}

bool Device::needsPolling() { return hasToBePolledForStatus; }

// static
const char* Device::getStatusAsString(DeviceStatus status) {
    switch (status) {
        case ON:
            return "ON";
        case OFF:
            return "OFF";
        case UNDEFINED:
            return "UNDEFINED";
        case SHUTTER_STOPPED:
            return "stopped";
        case SHUTTER_OPENING:
            return "opening";
        case SHUTTER_CLOSING:
            return "closing";
        case SHUTTER_OPENED:
            return "open";
        case SHUTTER_CLOSED:
            return "closed";
    }

    return NULL;
}

// static
const char* Device::getModeAsString(DeviceMode mode) {
    switch (mode) {
        case DIMMER:
            return "DIMMER";
        case ON_OFF:
            return "ON_OFF";
        case SHUTTER:
           return "SHUTTER";
        case NO_RCPT:
            return "NO_RCP";
    }

    return NULL;
}

// static
const char* Device::getAvailabilityAsString(DeviceAvailability availability) {
    switch (availability) {
        case ONLINE:
            return "Online";
        case OFFLINE:
            return "Offline";
    }

    return NULL;
}

void Device::setName(const char* n) {
    if (Yokis::validDeviceName(n)) Yokis::copyText(name, sizeof(name), n);
}
void Device::setHardwareAddress(const uint8_t* a) {
    if (a) { memcpy(hardwareAddress, a, sizeof(hardwareAddress)); addressConfigured = true; }
}
void Device::setHardwareAddress(const char* text) {
    uint32_t value;
    if (!text || strlen(text) != 4 || !Yokis::unsignedNumber(text, 65535, value, 16)) return;
    uint8_t a[] = {uint8_t(value >> 8), uint8_t(value), uint8_t(value >> 8), uint8_t(value), uint8_t(value)};
    setHardwareAddress(a);
}
void Device::setChannel(uint8_t c) {
    if (c <= 125) { channel = c; channelConfigured = true; }
}
void Device::setVersion(const uint8_t* v) { if (v) memcpy(version, v, sizeof(version)); }
void Device::setSerial(const uint8_t* v) { if (v) memcpy(serial, v, sizeof(serial)); }
bool Device::isConfigured() const {
    return addressConfigured && channelConfigured && Yokis::validDeviceName(name);
}
bool Device::isDuplicateCommand(Yokis::Command cmd, uint32_t now) const {
    return commandHistory.duplicate(cmd, now);
}
void Device::acknowledgeCommand(Yokis::Command cmd, uint32_t now) {
    commandHistory.acknowledged(cmd, now);
}

void Device::setMode(DeviceMode mode) { if (mode >= ON_OFF && mode <= SHUTTER) this->mode = mode; }

void Device::setMode(const char* mode) {
    if (mode == NULL || strlen(mode) == 0) {
        this->setMode(ON_OFF);
    } else if (strcmp("DIMMER", mode) == 0) {
        this->setMode(DIMMER);
    } else if (strcmp("SHUTTER", mode) == 0) {
        this->setMode(SHUTTER);
    } else if (strcmp("NO_RCPT", mode) == 0) {
        this->setMode(NO_RCPT);
    } else {
        this->setMode(ON_OFF);
    }
}

void Device::setStatus(DeviceStatus status) {
    this->status = status;
    if (this->status == OFF) this->setBrightness(BRIGHTNESS_OFF);
    this->lastUpdateMillis = millis();
}

void Device::setBrightness(DimmerBrightness brightness) {
    this->brightness = brightness;
    this->lastUpdateMillis = millis();
}

void Device::setAvailability(DeviceAvailability availability) {
    this->availability = availability;
}

void Device::online() { this->setAvailability(ONLINE); }
void Device::offline() {
    this->setAvailability(OFFLINE);
    this->setStatus(UNDEFINED);
}
bool Device::isOnline() { return this->availability == ONLINE; }
bool Device::isOffline() { return this->availability == OFFLINE; }

void Device::pollMePlease() { this->hasToBePolledForStatus = true; }

void Device::pollingSuccess() {
    this->hasToBePolledForStatus = false;
    this->failedPolls = 0;
}

uint8_t Device::pollingFailed() {
    this->hasToBePolledForStatus = false;
    if (failedPolls < 255) ++failedPolls;
    return failedPolls;
}

uint8_t Device::getFailedPollings() {
    return this->failedPolls;
}

void Device::toggleStatus() {
    switch (status) {
        case UNDEFINED:
            setStatus(UNDEFINED);
            break;
        case SHUTTER_STOPPED:
            setStatus(SHUTTER_STOPPED);
            break;
        case OFF:
            setStatus(ON);
            break;
        case ON:
            setStatus(OFF);
            setBrightness(BRIGHTNESS_OFF);
            break;
        default: // A logical toggle cannot infer a physical shutter position.
            setStatus(UNDEFINED);
            break;
    }
}

void Device::toSerial() {
    LOG.print(name);
    LOG.print(" - status=");
    LOG.println(Device::getStatusAsString(status));
    LOG.print("Availability: ");
    LOG.println(Device::getAvailabilityAsString(availability));
    LOG.print("mode: ");
    LOG.println(Device::getModeAsString(mode));
    LOG.print("hw: ");
    LOG.print(hardwareAddress[0], HEX);
    LOG.print(" ");
    LOG.println(hardwareAddress[1], HEX);
    LOG.print("channel: ");
    LOG.println(channel, HEX);
    LOG.print("serial/version: ");
    LOG.print(serial[0], HEX);
    LOG.print(" ");
    LOG.print(serial[1], HEX);
    LOG.print("/");
    LOG.print(version[0], HEX);
    LOG.print(" ");
    LOG.print(version[1], HEX);
    LOG.print(" ");
    LOG.println(version[2], HEX);
}

// Copy all fields from given device to this device
void Device::copy(const Device* d) { if (d && d != this) *this = *d; }

// Static - Get device from a given list of devices
// devices is the list of devices
// size is the size of the list given
// deviceName is the name of the device to look for
// returns a pointer to the corresponding device if found, NULL otherwise
Device* Device::getFromList(Device** devices, size_t size,
                            const char* deviceName) {
    Device* d = NULL;
    if (!devices || !deviceName) return NULL;

    for (unsigned int i = 0; i < size; i++) {
        if (devices[i] != NULL &&
            strcmp(devices[i]->getName(), deviceName) == 0) {
            d = devices[i];
            break;
        }
    }

    return d;
}

#ifdef ESP8266
namespace {
// Consume the ENTIRE physical line even when too long. Never parse its tail as
// another device. CRLF and a last line without LF remain backward compatible.
bool readConfigLine(File& f, char* buf, size_t cap) {
    size_t n = 0; bool overflow = false;
    while (f.available()) {
        int c = f.read();
        if (c == '\n') break;
        if (n + 1 < cap) buf[n++] = char(c); else overflow = true;
    }
    if (n && buf[n - 1] == '\r') --n;
    buf[n] = 0;
    return !overflow;
}
}

bool Device::parseConfigLine(const char* line, Device& result) {
    if (!line || strlen(line) >= Yokis::ConfigLineSize) return false;
    char buf[Yokis::ConfigLineSize]; strcpy(buf, line);
    size_t len = strlen(buf);
    while (len && (buf[len-1] == '\r' || buf[len-1] == '\n')) buf[--len] = 0;
    char* fields[8]; fields[0] = buf;
    for (unsigned i = 1; i < 8; ++i) {
        char* sep = strchr(fields[i - 1], '|');
        if (!sep) return false;
        *sep = 0; fields[i] = sep + 1;
    }
    if (strchr(fields[7], '|') || !Yokis::validDeviceName(fields[0])) return false;
    const size_t lengths[] = {0,4,2,2,2,6,4,4};
    uint32_t values[8] = {};
    for (unsigned i = 1; i < 8; ++i) {
        if (strlen(fields[i]) != lengths[i] ||
            !Yokis::unsignedNumber(fields[i], i == 5 ? 0xffffffUL : 65535,
                                   values[i], i == 7 ? 10 : 16)) return false;
    }
    if (values[2] > 125 || values[7] > SHUTTER) return false;
    Device d(fields[0]);
    d.setHardwareAddress(fields[1]); d.setChannel(uint8_t(values[2]));
    uint8_t ver[] = {uint8_t(values[5] >> 16), uint8_t(values[5] >> 8), uint8_t(values[5])};
    uint8_t ser[] = {uint8_t(values[6] >> 8), uint8_t(values[6])};
    d.setVersion(ver); d.setSerial(ser); d.setMode(DeviceMode(values[7]));
    result.copy(&d);
    return true;
}

bool Device::formatConfigLine(char* buf, size_t size) const {
    if (!isConfigured() || !buf || !size) return false;
    int n = snprintf(buf, size, "%s|%02x%02x|%02x|00|00|%02x%02x%02x|%02x%02x|%04u",
        name, hardwareAddress[0], hardwareAddress[1], channel,
        version[0], version[1], version[2], serial[0], serial[1], unsigned(mode));
    return n >= 0 && size_t(n) < size;
}

bool Device::writeConfig(const Device* replacement, const char* removedName, bool clear) {
    if (!YokisLittleFS::init()) return false;
    const char* temp = "/yokis.conf.tmp";
    File input;
    if (!clear && LittleFS.exists(LITTLEFS_CONFIG_FILENAME)) {
        input = LittleFS.open(LITTLEFS_CONFIG_FILENAME, "r");
        if (!input) return false;
    }
    File output = LittleFS.open(temp, "w");
    if (!output) return false;
    bool ok = true; char line[Yokis::ConfigLineSize]; unsigned count = 0;
    while (input && input.available() && ok) {
        if (!readConfigLine(input, line, sizeof(line))) { ok = false; break; }
        if (!line[0]) continue;
        Device d("");
        if (!parseConfigLine(line, d)) { ok = false; break; }
        if ((removedName && strcmp(d.getName(), removedName) == 0) ||
            (replacement && strcmp(d.getName(), replacement->getName()) == 0)) continue;
        if (++count > 64 || output.println(line) != strlen(line) + 2) ok = false;
    }
    if (replacement && ok) {
        ok = ++count <= 64 && replacement->formatConfigLine(line, sizeof(line));
        if (ok) ok = output.println(line) == strlen(line) + 2;
    }
    output.flush(); ok = ok && !output.getWriteError();
    input.close(); output.close();
    // LittleFS rename replaces the old file atomically; never remove it first.
    if (ok) ok = LittleFS.rename(temp, LITTLEFS_CONFIG_FILENAME);
    if (!ok) { LittleFS.remove(temp); LOG.println("Configuration unchanged: write/validation failed"); }
    return ok;
}

bool Device::storeRawConfig(const char* line) {
    Device d(""); return parseConfigLine(line, d) && d.saveToLittleFS();
}
bool Device::saveToLittleFS() { return isConfigured() && writeConfig(this, NULL, false); }
bool Device::deleteFromConfig(const char* name) {
    return Yokis::validDeviceName(name) && writeConfig(NULL, name, false);
}
bool Device::clearConfigFromLittleFS() { return writeConfig(NULL, NULL, true); }

int Device::loadFromLittleFS(Device** devices, const unsigned int size) {
    if (!devices || !YokisLittleFS::init()) return -1;
    if (!LittleFS.exists(LITTLEFS_CONFIG_FILENAME)) return 0;
    File f = LittleFS.open(LITTLEFS_CONFIG_FILENAME, "r");
    if (!f) return -1;
    unsigned n = 0; bool ok = true; char line[Yokis::ConfigLineSize];
    while (f.available()) {
        if (!readConfigLine(f, line, sizeof(line))) { ok = false; break; }
        if (!line[0]) continue;
        Device d("");
        if (n >= size || !parseConfigLine(line, d) || getFromList(devices, n, d.getName())) {
            ok = false; break;
        }
        devices[n] = new Device(&d);
        if (!devices[n]) { ok = false; break; }
        ++n;
    }
    f.close();
    if (!ok) {
        for (unsigned i = 0; i < n; ++i) { delete devices[i]; devices[i] = NULL; }
        LOG.println("Invalid device configuration: existing in-memory devices preserved");
        return -1;
    }
    return int(n);
}
void Device::displayConfigFromLittleFS() {
    if (!YokisLittleFS::init()) return;
    File f = LittleFS.open(LITTLEFS_CONFIG_FILENAME, "r");
    LOG.println("LittleFS configuration stored:");
    while (f && f.available()) LOG.write(f.read());
    f.close();
}
#endif

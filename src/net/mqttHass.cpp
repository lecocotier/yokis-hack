#ifdef ESP8266
#include "net/mqttHass.h"
#include "RF/device.h"
#include "globals.h"
#include "reliability.h"

MqttHass::MqttHass(WiFiClient& wifiClient) : Mqtt(wifiClient) {
    MqttConfig config = MqttConfig::loadFromLittleFS();

    if(!config.isEmpty()) {
        this->setConnectionInfo(config, false);
    }
}

MqttHass::MqttHass(WiFiClient& wifiClient, const char* host,
                   const uint16_t port, const char* username,
                   const char* password)
    : Mqtt(wifiClient) {
        this->setConnectionInfo(host, port, username, password, false);
}

bool MqttHass::isDiscoveryDone() {
    return discoveryDone;
}

void MqttHass::setDiscoveryDone(bool status) {
    discoveryDone = status;
    discoveryIndex_ = replayIndex_ = 0;
    replayPending_ = false;
    refreshClockStarted_ = false;
}

void MqttHass::connectionEstablished() {
    birthSubscribed_ = false;
    setDiscoveryDone(false);
}

bool MqttHass::handleBirth(const char* topic, const uint8_t* payload, unsigned int length) {
    if (!topic || strcmp(topic, HASS_BIRTH_TOPIC)) return false;
    if (payload && length == strlen(HASS_BIRTH_PAYLOAD) &&
        memcmp(payload, HASS_BIRTH_PAYLOAD, length) == 0 && !refreshPending()) {
        setDiscoveryDone(false);
        LOG.println("Home Assistant birth: scheduled discovery and cached-state refresh");
    }
    return true; // the reserved birth topic is never a radio command
}

void MqttHass::serviceRefresh(Device* const* devices, size_t count) {
    if (!connected() || !devices || count > MQTT_MAX_NUM_OF_YOKIS_DEVICES) return;
    // One device per step, minimum 100ms. Commands/post-STOP checks are serviced
    // by the caller first. No RF transaction is performed in this method.
    if (refreshClockStarted_ && !Yokis::elapsed(millis(), lastRefreshAt_, 100)) return;
    refreshClockStarted_ = true; lastRefreshAt_ = millis();
    if (!birthSubscribed_) {
        // Kept outside the device subscription list: reloading devices neither
        // removes this subscription nor repeatedly replays a retained birth.
        birthSubscribed_ = PubSubClient::subscribe(HASS_BIRTH_TOPIC);
        if (!birthSubscribed_) return;
    }
    if (!discoveryDone) {
        while (discoveryIndex_ < count && !devices[discoveryIndex_]) ++discoveryIndex_;
        if (discoveryIndex_ < count) {
            Device* d = devices[discoveryIndex_];
            if (!publishDevice(d) || !subscribeDevice(d)) return;
            ++discoveryIndex_;
            if (d->getMode() != NO_RCPT) d->pollMePlease();
        }
        while (discoveryIndex_ < count && !devices[discoveryIndex_]) ++discoveryIndex_;
        if (discoveryIndex_ == count) {
            discoveryDone = true; replayPending_ = true; replayIndex_ = 0;
            LOG.println("MQTT discovery submitted; cached-state replay scheduled");
        }
        return;
    }
    if (!replayPending_) return;
    while (replayIndex_ < count && !devices[replayIndex_]) ++replayIndex_;
    if (replayIndex_ < count) {
        Device* d = devices[replayIndex_];
        // Replay is marked and never calls setStatus/setBrightness: no invented
        // radio observation or reset of the last status timestamp.
        if (!notifyAvailability(d, d->isOffline() ? "Offline" : "Online")) return;
        const bool ok = d->getMode() == DIMMER ? notifyBrightness(d, true) : notifyPower(d, true);
        if (!ok) return;
        ++replayIndex_;
    }
    while (replayIndex_ < count && !devices[replayIndex_]) ++replayIndex_;
    if (replayIndex_ == count) replayPending_ = false;
}

char* MqttHass::availabilityJson(char* buf, size_t size) {
    const int n = snprintf(buf, size,
        "\"avty\":[{\"topic\":\"~tele/LWT\",\"pl_avail\":\"Online\",\"pl_not_avail\":\"Offline\"},"
        "{\"topic\":\"%s\",\"pl_avail\":\"Online\",\"pl_not_avail\":\"Offline\"}],\"avty_mode\":\"all\",", gatewayTopic());
    return n >= 0 && size_t(n) < size ? buf : NULL;
}

// Existing unique IDs, topics, names and shutter template are intentionally stable.
char* MqttHass::newMessageJson(const Device* device, char* buf) {
    if (!device || !buf) return NULL;
    char availability[256];
    if (!availabilityJson(availability, sizeof(availability))) return NULL;
    const char* name = device->getName();
    const bool shutter = device->getMode() == SHUTTER;
    const bool dimmer = device->getMode() == DIMMER;
    const char* modeFields = shutter ?
        "\"optimistic\":true,\"val_tpl\":\"{{ 'None' if value_json.POWER == 'stopped' else value_json.POWER }}\","
        "\"json_attr_t\":\"~tele/DETAIL\",\"payload_open\":\"ON\",\"payload_close\":\"OFF\",\"payload_stop\":\"PAUSE\"," :
        "\"optimistic\":false,\"stat_val_tpl\":\"{{value_json.POWER}}\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\",";
    const char* dimmerFields = dimmer ?
        "\"on_cmd_type\":\"brightness\",\"bri_cmd_t\":\"~cmnd/BRIGHTNESS\",\"bri_scl\":4,"
        "\"bri_stat_t\":\"~tele/BRIGHTNESS\",\"bri_val_tpl\":\"{{value_json.BRIGHTNESS}}\"," : "";
    char displayName[Yokis::DeviceNameMax + 16];
    snprintf(displayName, sizeof(displayName), shutter ? "Shutter %s" : dimmer ? "%s dimmer" : "%s switch", name);
    const int n = snprintf(buf, MQTT_MAX_PACKET_SIZE,
        "{\"name\":\"%s\",\"cmd_t\":\"~cmnd/POWER\",\"stat_t\":\"~tele/STATE\",%s%s%s"
        "\"uniq_id\":\"esp-%s\",\"dev\":{\"name\":\"%s\",\"ids\":[\"esp-%s\"],\"mdl\":\"%s\",\"mf\":\"Yokis\"},\"~\":\"%s/\"}",
        displayName, modeFields, dimmerFields, availability, name, name, name,
        shutter ? "MVR500ERP " : dimmer ? "MTV500ERX" : "MTR2000ERX", name);
    return n >= 0 && size_t(n) < MQTT_MAX_PACKET_SIZE ? buf : NULL;
}

char* MqttHass::newPublishTopic(const Device* device, char* buf) {
    if (device->getMode() == SHUTTER) { 
        snprintf(buf, 96, "%s/cover/%s/config", HASS_PREFIX, device->getName());
    } else {
        snprintf(buf, 96, "%s/light/%s/config", HASS_PREFIX, device->getName());
    }
    return buf;
}

// Publish device to MQTT for HASS discovery
bool MqttHass::publishDevice(const Device* device) {
    bool ret;
    char topic[128];
    char payload[MQTT_MAX_PACKET_SIZE];

    if (!device || !device->isConfigured()) return false;
    newPublishTopic(device, topic);
    if (!newMessageJson(device, payload) ||
        strlen(topic) + strlen(payload) + 7 > MQTT_MAX_PACKET_SIZE) {
        LOG.println("MQTT discovery too large; nothing published"); return false;
    }

    LOG.print("Sending MQTT message, topic_len=");
    LOG.print(strlen(topic));
    LOG.print(", payload_len=");
    LOG.println(strlen(payload));

    ret = this->publish(topic, payload, true);
    if (ret) ret = notifyAvailability(device, device->getAvailability() == OFFLINE ? "Offline" : "Online");


    return ret;
}

bool MqttHass::notifyAvailability(const Device* device, const char* status) {
    char buf[96];

    snprintf(buf, 96, "%s/tele/LWT", device->getName());
    return publish(buf, status, true);
}

bool MqttHass::notifyOnline(const Device* device) {
    return notifyAvailability(device, "Online");
}

bool MqttHass::notifyOffline(const Device* device) {
    return notifyAvailability(device, "Offline");
}

bool MqttHass::notifyPower(const Device* device, bool replayed) {
    return notifyPower(device, device->getStatus(), replayed);
}

bool MqttHass::notifyPower(const Device* device, DeviceStatus ds, bool replayed) {
    char buf[96];
    char bufPayload[64];

    snprintf(buf, 96, "%s/tele/STATE", device->getName());
    sprintf(bufPayload, "{\"POWER\":\"%s\"}", (device->getMode() == SHUTTER && ds == UNDEFINED) ? "None" : Device::getStatusAsString(ds));
    bool ok = publish(buf, bufPayload, true);
    if (device->getMode() == SHUTTER) {
        const Yokis::ShutterFeedback& feedback = device->shutterFeedback();
        char detail[512];
        char raw[6] = "none";
        if (feedback.rawValid()) snprintf(raw, sizeof(raw), "%02X %02X", feedback.raw0(), feedback.raw1());
        const bool known = ds != UNDEFINED;
        const int written = snprintf(detail, sizeof(detail),
            "{\"yokis_state\":\"%s\",\"state_source\":\"%s\",\"state_estimated\":%s,"
            "\"last_command\":\"%s\",\"last_command_ms\":%lu,\"command_response\":%s,"
            "\"raw_response\":\"%s\",\"raw_origin\":\"%s\","
            "\"stop_check\":\"%s\",\"stop_check_attempts\":%u,\"state_replayed\":%s,\"status_age_ms\":%lu}",
            known ? Device::getStatusAsString(ds) : "unknown",
            known ? feedback.sourceName() : "unknown",
            known && feedback.estimated() ? "true" : "false", feedback.commandName(),
            (unsigned long)feedback.commandAt(), feedback.commandResponse() ? "true" : "false",
            raw, feedback.rawOrigin(), feedback.verification().name(),
            unsigned(feedback.verification().attempts()), replayed ? "true" : "false",
            (unsigned long)(uint32_t(millis()) - uint32_t(device->getLastUpdateMillis())));
        if (written < 0 || size_t(written) >= sizeof(detail)) return false;
        snprintf(buf, sizeof(buf), "%s/tele/DETAIL", device->getName());
        ok = publish(buf, detail, true) && ok;
    }
    return ok;
}

bool MqttHass::notifyBrightness(const Device* device, bool replayed) {
    char buf[96];
    char bufPayload[64];

    bool ok = notifyPower(device, (device->getBrightness() == BRIGHTNESS_OFF ? OFF : ON), replayed);

    snprintf(buf, 96, "%s/tele/BRIGHTNESS", device->getName());
    sprintf(bufPayload, "{\"BRIGHTNESS\":\"%d\"}", device->getBrightness());
    return publish(buf, bufPayload, true) && ok;
}

// Subscribe device to be able to be controlled over MQTT
bool MqttHass::subscribeDevice(const Device* device) {
    if (!device || !device->isConfigured()) return false;
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/cmnd/POWER", device->getName());
    bool ok = subscribe(topic);
    if (device->getMode() == DIMMER) {
        snprintf(topic, sizeof(topic), "%s/cmnd/BRIGHTNESS", device->getName());
        ok = subscribe(topic) && ok;
    }
    return ok;
}
void MqttHass::removeDiscovery(const Device* device) {
    char topic[128]; newPublishTopic(device, topic); publish(topic, "", true);
    const char* suffixes[] = {"STATE", "DETAIL", "BRIGHTNESS", "LWT"};
    for (const char* suffix : suffixes) {
        snprintf(topic, sizeof(topic), "%s/tele/%s", device->getName(), suffix);
        publish(topic, "", true);
    }
}
#endif
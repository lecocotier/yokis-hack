#ifdef ESP8266
#include "net/mqttHass.h"
#include "RF/device.h"
#include "globals.h"

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
}

// Get JSON message to publish for HASS discovery
char* MqttHass::newMessageJson(const Device* device, char* buf) {
    if (device->getMode() == DIMMER) {
        snprintf(buf, MQTT_MAX_PACKET_SIZE,
                "{"
                "\"name\":\"%s dimmer\","
                "\"optimistic\":false,"  // if false, cannot know the status
                                             // of the device
                "\"on_command_type\":\"brightness\","  // only send brightness
                "\"bri_cmd_t\":\"~cmnd/BRIGHTNESS\","
                "\"bri_scl\":\"4\","  // 0=toggle, 1=min, 2=mid, 3/4=max
                "\"bri_stat_t\":\"~tele/BRIGHTNESS\","
                "\"bri_val_tpl\":\"{{value_json.BRIGHTNESS}}\","
                "\"cmd_t\":\"~cmnd/POWER\","
                "\"pl_off\":\"OFF\","
                "\"state_topic\":\"~tele/STATE\","
                "\"state_value_template\":\"{{value_json.POWER}}\","
                "\"avty_t\":\"~tele/LWT\","
                "\"pl_avail\":\"Online\","
                "\"pl_not_avail\":\"Offline\","
                "\"uniq_id\":\"esp-%s\","
                "\"device\":{"
                "\"name\":\"%s\","
                "\"identifiers\":[\"esp-%s\"],"
                "\"model\":\"MTV500ERX\","
                "\"mf\":\"Yokis\""
                "},"
                "\"~\":\"%s/\""
                "}",
                device->getName(), device->getName(), device->getName(),
                device->getName(), device->getName());
        } else if (device->getMode() == SHUTTER) {
        snprintf(buf, MQTT_MAX_PACKET_SIZE,
                "{"
                "\"name\":\"Shutter %s\","
                "\"optimistic\":false,"  // if false, cannot know the status
                                             // of the device
                "\"cmd_t\":\"~cmnd/POWER\","
                "\"state_topic\":\"~tele/STATE\","
                "\"val_tpl\":\"{{value_json.POWER}}\","
//                "\"tilt_status_topic\": \"~tele/TILT\","
//                "\"tilt_status_template\":\"{{value_json.TILT}}\","
                "\"avty_t\":\"~tele/LWT\","
                "\"pl_avail\":\"Online\","
                "\"pl_not_avail\":\"Offline\","
                "\"payload_close\":\"OFF\","
                "\"payload_open\":\"ON\","
                "\"payload_stop\":\"PAUSE\","
                "\"uniq_id\":\"esp-%s\","
                "\"device\":{"
                "\"name\":\"%s\","
                "\"identifiers\":[\"esp-%s\"],"
                "\"model\":\"MVR500ERP \","
                "\"mf\":\"Yokis\""
                "},"
                "\"~\":\"%s/\""
                "}",
                device->getName(), device->getName(), device->getName(),
                device->getName(), device->getName());
        } else {
        snprintf(buf, MQTT_MAX_PACKET_SIZE,
                "{"
                "\"name\":\"%s switch\","
                "\"optimistic\":false,"  // if false, cannot know the status
                                             // of the device
                "\"cmd_t\":\"~cmnd/POWER\","
                "\"state_topic\":\"~tele/STATE\","
                "\"state_value_template\":\"{{value_json.POWER}}\","
                "\"pl_off\":\"OFF\","
                "\"pl_on\":\"ON\","
                "\"avty_t\":\"~tele/LWT\","
                "\"pl_avail\":\"Online\","
                "\"pl_not_avail\":\"Offline\","
                "\"uniq_id\":\"esp-%s\","
                "\"device\":{"
                "\"name\":\"%s\","
                "\"identifiers\":[\"esp-%s\"],"
                "\"model\":\"MTR2000ERX\","
                "\"mf\":\"Yokis\""
                "},"
                "\"~\":\"%s/\""
                "}",
                device->getName(), device->getName(), device->getName(),
                device->getName(), device->getName());
    }

    return buf;
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
    newMessageJson(device, payload);

    LOG.print("Sending MQTT message, topic_len=");
    LOG.print(strlen(topic));
    LOG.print(", payload_len=");
    LOG.println(strlen(payload));

    ret = this->publish(topic, payload, true);
    if (ret) {
        notifyOnline(device);
    }


    return ret;
}

void MqttHass::notifyAvailability(const Device* device, const char* status) {
    char buf[96];

    snprintf(buf, 96, "%s/tele/LWT", device->getName());
    publish(buf, status, true);
}

void MqttHass::notifyOnline(const Device* device) {
    notifyAvailability(device, "Online");
}

void MqttHass::notifyOffline(const Device* device) {
    notifyAvailability(device, "Offline");
}

void MqttHass::notifyPower(const Device* device) {
    notifyPower(device, device->getStatus());
}

void MqttHass::notifyPower(const Device* device, DeviceStatus ds) {
    char buf[96];
    char bufPayload[64];

    snprintf(buf, 96, "%s/tele/STATE", device->getName());
    sprintf(bufPayload, "{\"POWER\":\"%s\"}", (device->getMode() == SHUTTER && ds == UNDEFINED) ? "None" : Device::getStatusAsString(ds));
    publish(buf, bufPayload, false);
}

void MqttHass::notifyBrightness(const Device* device) {
    char buf[96];
    char bufPayload[64];

    notifyPower(device, (device->getBrightness() == BRIGHTNESS_OFF ? OFF : ON));

    snprintf(buf, 96, "%s/tele/BRIGHTNESS", device->getName());
    sprintf(bufPayload, "{\"BRIGHTNESS\":\"%d\"}", device->getBrightness());
    publish(buf, bufPayload, false);
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
}
#endif

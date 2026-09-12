#ifdef ESP8266
#ifndef __MQTT_HASS_H__
#define __MQTT_HASS_H__

// Home assistant MQTT discovery
// See https://www.home-assistant.io/docs/mqtt/discovery

#include <Arduino.h>
#include <ESP8266WiFi.h>

#include "RF/device.h"
#include "net/mqtt.h"

#define HASS_PREFIX "homeassistant"
#ifndef HASS_BIRTH_TOPIC
#define HASS_BIRTH_TOPIC HASS_PREFIX "/status"
#endif
#ifndef HASS_BIRTH_PAYLOAD
#define HASS_BIRTH_PAYLOAD "online"
#endif

class MqttHass : public Mqtt {
   protected:
    char* newMessageJson(const Device*, char*);
    char* newPublishTopic(const Device*, char*);
    bool discoveryDone = false;
    void connectionEstablished() override;
    bool birthSubscribed_ = false;
    bool replayPending_ = false;
    bool refreshClockStarted_ = false;
    uint8_t discoveryIndex_ = 0, replayIndex_ = 0;
    uint32_t lastRefreshAt_ = 0;
    char* availabilityJson(char*, size_t);

   public:
    MqttHass(WiFiClient&);
    MqttHass(WiFiClient&, const char*, const uint16_t, const char*, const char*);
    bool isDiscoveryDone();
    void setDiscoveryDone(bool);
    // Calls only publish/subscribe or request polling, never performs RF work.
    void serviceRefresh(Device* const*, size_t);
    bool handleBirth(const char*, const uint8_t*, unsigned int);
    bool refreshPending() const { return !discoveryDone || replayPending_; }
    void removeDiscovery(const Device*);
    bool publishDevice(const Device*);
    bool subscribeDevice(const Device*);
    bool notifyAvailability(const Device*, const char*);
    bool notifyOnline(const Device*);
    bool notifyOffline(const Device*);
    bool notifyPower(const Device*, bool replayed=false);
    bool notifyPower(const Device*, DeviceStatus, bool replayed=false);
    bool notifyBrightness(const Device* device, bool replayed=false);
};

#endif  // __MQTT_HASS_H__
#endif  // ESP8266

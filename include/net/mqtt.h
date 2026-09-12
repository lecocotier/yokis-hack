#ifdef ESP8266
#ifndef __MQTT_H__
#define __MQTT_H__

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "net/mqttConfig.h"

#define MQTT_TOPIC_COMMAND "cmnd"
#define MQTT_MAX_NUM_OF_YOKIS_DEVICES 64
#define MQTT_MAX_SUBSCRIPTIONS (2 * MQTT_MAX_NUM_OF_YOKIS_DEVICES)
#define MQTT_CONNECT_RETRY_EVERY_MS 2000

class Mqtt : public PubSubClient, public MqttConfig {
   private:
    uint32_t lastConnectionRetry = 0;
    bool connectionAttempted = false;
    char* subscribedTopics[MQTT_MAX_SUBSCRIPTIONS];
    uint16_t subscribedTopicIdx;
    void resubscribe();
    static void callback(char*, uint8_t*, unsigned int);

   protected:
    virtual void connectionEstablished() {}

   public:
    Mqtt(WiFiClient&);
    Mqtt(WiFiClient&, MqttConfig&);
    virtual ~Mqtt();
    bool setConnectionInfo(MqttConfig&, bool=true);
    bool setConnectionInfo(const char*, uint16_t, const char*, const char*, bool=true);
    boolean subscribe(const char*);
    void clearSubscriptions();
    bool reconnect(bool=false);
    boolean loop();
};

#endif
#endif

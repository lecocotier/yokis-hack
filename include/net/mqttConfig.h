#ifdef ESP8266
#ifndef __MQTT_CONFIG_H__
#define __MQTT_CONFIG_H__

#include <LittleFS.h>
#include <WString.h>
#include <stdlib.h>

#include "storage/yokisLittleFS.h"

#define MQTT_DEFAULT_PORT 1883
#define MQTT_CONFIG_FILE_NAME "/mqtt.conf"
#define MQTT_HOST_MAX_LENGTH 64
#define MQTT_USERNAME_MAX_LENGTH 32
#define MQTT_PASSWORD_MAX_LENGTH 32

class MqttConfig {
    private:
     char host[MQTT_HOST_MAX_LENGTH];
     uint16_t port;
     char username[MQTT_USERNAME_MAX_LENGTH];
     char password[MQTT_PASSWORD_MAX_LENGTH];

    public:
     MqttConfig();
     MqttConfig(const MqttConfig& config);
     MqttConfig& operator=(const MqttConfig&) = default;
     MqttConfig(const char*, uint16_t, const char*, const char*);
     ~MqttConfig();
     bool setHost(const char*);
     bool setPort(uint16_t);
     bool setUsername(const char*);
     bool setPassword(const char*);
     char* getHost();
     uint16_t getPort();
     char* getUsername();
     char* getPassword();
     bool isEmpty();
     void printDebug(Print&);

     // Configuration persistence
     bool saveToLittleFS();
     static MqttConfig loadFromLittleFS();
     static bool deleteConfigFromLittleFS();

};

#endif  // __MQTT_CONFIG_H__
#endif  // ESP8266

#ifdef ESP8266
#include "net/mqtt.h"
#include <Arduino.h>
#include "globals.h"
#include "reliability.h"

Mqtt::Mqtt(WiFiClient& wifiClient) : PubSubClient(wifiClient), MqttConfig(), transport_(wifiClient) {
    // Init subscriptions to NULL
    subscribedTopicIdx = 0;
    for (uint16_t i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        subscribedTopics[i] = NULL;
    }
}

Mqtt::Mqtt(WiFiClient& wifiClient, MqttConfig& mqttConfig)
    : PubSubClient(wifiClient), MqttConfig(mqttConfig), transport_(wifiClient) {
    this->setCallback(Mqtt::callback);

    // Init subscriptions to NULL
    subscribedTopicIdx = 0;
    for (uint16_t i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        subscribedTopics[i] = NULL;
    }
}

// Set mqtt connection info, and optionally save config to LittleFS (default to
// true)
bool Mqtt::setConnectionInfo(MqttConfig& c, bool saveConfig) {
    return setConnectionInfo(c.getHost(), c.getPort(), c.getUsername(), c.getPassword(), saveConfig);
}
bool Mqtt::setConnectionInfo(const char* host, uint16_t port, const char* username,
                             const char* password, bool saveConfig) {
    MqttConfig candidate;
    if (!candidate.setHost(host) || !candidate.setPort(port) ||
        !candidate.setUsername(username) || !candidate.setPassword(password)) return false;
    if (saveConfig && !candidate.saveToLittleFS()) return false;
    if (connected()) disconnect();
    MqttConfig::operator=(candidate);
    setServer(getHost(), getPort());
    connectionAttempted = false;
    // Connection is attempted from loop(), not from an AsyncTCP callback.
    return true;
}
boolean Mqtt::subscribe(const char* topic) {
    if (!topic || !*topic || !connected()) return false;
    for (uint16_t i = 0; i < subscribedTopicIdx; ++i)
        if (strcmp(subscribedTopics[i], topic) == 0) return PubSubClient::subscribe(topic);
    if (subscribedTopicIdx >= MQTT_MAX_SUBSCRIPTIONS) return false;
    char* saved = (char*)malloc(strlen(topic) + 1);
    if (!saved) return false;
    strcpy(saved, topic);
    if (!PubSubClient::subscribe(topic)) { free(saved); return false; }
    subscribedTopics[subscribedTopicIdx++] = saved;
    return true;
}

void Mqtt::resubscribe() {
    LOG.print("Resubscribing, #topics=");
    LOG.println(subscribedTopicIdx);
    for(uint16_t i=0; i<subscribedTopicIdx; i++) {
        PubSubClient::subscribe(subscribedTopics[i]);
    }
}

void Mqtt::clearSubscriptions() {
    for (uint16_t i = 0; i < MQTT_MAX_SUBSCRIPTIONS; ++i) {
        if (subscribedTopics[i]) {
            if (connected()) PubSubClient::unsubscribe(subscribedTopics[i]);
            free(subscribedTopics[i]); subscribedTopics[i] = NULL;
        }
    }
    subscribedTopicIdx = 0;
}
Mqtt::~Mqtt() { clearSubscriptions(); }

// force=false by default
bool Mqtt::reconnect(bool force) {
    // No configuration available
    if (this->MqttConfig::isEmpty()) {
        return false;
    }

    uint32_t now = millis();
    if (!force && connectionAttempted && !Yokis::elapsed(now, lastConnectionRetry, MQTT_CONNECT_RETRY_EVERY_MS))
        return false;
    lastConnectionRetry = now; connectionAttempted = true;

    char buf[128];
    String clientId = "YokisHack-";
    clientId += String(random(0xffff), HEX);

    snprintf(buf, sizeof(buf), "Connecting to MQTT %s:%hu with client ID=%s... ", getHost(), getPort(), clientId.c_str());
    LOG.print(buf);

    if (this->connect(clientId.c_str(), getUsername(), getPassword())) {
        LOG.println("connected");
        connectionEstablished();
        this->resubscribe();  // resubscribe to all configured topics
    } else {
        LOG.print("failed with state ");
        LOG.println(this->state());
    }

    return this->connected();
}

boolean Mqtt::loop() {
    inputHandled_ = false;
    if (this->MqttConfig::isEmpty()) {
        return false;
    }

    if(!this->connected()) {
        this->reconnect();
    }

    // PubSubClient handles one packet per invocation. Main loop yields to
    // another packet/command before starting an automatic radio query.
    inputHandled_ = hasPendingInput();
    return PubSubClient::loop();
}

// static - default callback logging on Serial
void Mqtt::callback(char* topic, uint8_t* payload, unsigned int length) {
    LOG.print("Message topic: ");
    LOG.println(topic);

    LOG.print("Message payload: ");
    for (unsigned int i = 0; i < length; i++) {
        LOG.print((char)payload[i]);
    }

    LOG.println();
    LOG.println("-----------------------");
}

#endif

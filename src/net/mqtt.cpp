#ifdef ESP8266
#include "net/mqtt.h"
#include <Arduino.h>
#include "globals.h"
#include "reliability.h"

Mqtt::Mqtt(WiFiClient& wifiClient) : PubSubClient(wifiClient), MqttConfig(), transport_(wifiClient) {
    initializeIdentity();
    // Init subscriptions to NULL
    subscribedTopicIdx = 0;
    for (uint16_t i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        subscribedTopics[i] = NULL;
    }
}

Mqtt::Mqtt(WiFiClient& wifiClient, MqttConfig& mqttConfig)
    : PubSubClient(wifiClient), MqttConfig(mqttConfig), transport_(wifiClient) {
    this->setCallback(Mqtt::callback);

    initializeIdentity();
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
Mqtt::~Mqtt() { disconnect(); clearSubscriptions(); }

void Mqtt::initializeIdentity() {
    // Chip identity is stable across reconnects/reboots; no saved config migration.
    snprintf(clientId_, sizeof(clientId_), "YokisHack-%06lx", (unsigned long)ESP.getChipId());
    snprintf(gatewayTopic_, sizeof(gatewayTopic_), "yokis/%s/availability", clientId_);
    setSocketTimeout(2); // Bound MQTT response waits; TCP/DNS have their own timeouts.
}

void Mqtt::announceGateway() {
    if (connected() && gatewayOnlinePending_) {
        lastGatewayPublish_ = millis();
        if (publish(gatewayTopic_, "Online", true)) gatewayOnlinePending_ = false;
    }
}

void Mqtt::disconnect() {
    if (connected()) {
        // A clean MQTT disconnect cancels the will. Publish Offline first.
        if (publish(gatewayTopic_, "Offline", true)) PubSubClient::disconnect();
        else transport_.stop(); // leave an unclean session so the broker uses the will
    }
    gatewayOnlinePending_ = true;
}

// force=false by default
bool Mqtt::reconnect(bool force) {
    // No configuration available
    if (this->MqttConfig::isEmpty() || WiFi.status() != WL_CONNECTED) return false;
    if (connected()) return true;

    uint32_t now = millis();
    if (!force && connectionAttempted && !Yokis::elapsed(now, lastConnectionRetry, MQTT_CONNECT_RETRY_EVERY_MS))
        return false;
    lastConnectionRetry = now; connectionAttempted = true;

    char buf[128];

    snprintf(buf, sizeof(buf), "Connecting to MQTT %s:%hu with client ID=%s... ", getHost(), getPort(), clientId_);
    LOG.print(buf);

    if (this->connect(clientId_, getUsername(), getPassword(),
                      gatewayTopic_, 1, true, "Offline")) {
        LOG.println("connected");
        gatewayOnlinePending_ = true;
        announceGateway();
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

    if (WiFi.status() != WL_CONNECTED) {
        // Do not block in a broker connect while the station is disconnected.
        // No MQTT DISCONNECT: the broker will publish the gateway's will.
        if (connected()) transport_.stop();
        gatewayOnlinePending_ = true;
        return false;
    }
    if (!connected()) reconnect();
    if (gatewayOnlinePending_ && Yokis::elapsed(millis(), lastGatewayPublish_, 500))
        announceGateway();

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

#ifdef ESP8266
#include "net/webserver.h"
#include "globals.h"
#include "reliability.h"

namespace {
bool pending = false, pendingWifi = false, pendingMqtt = false;
uint32_t pendingAt = 0;
String nextSsid, nextPassword;
#ifdef MQTT_ENABLED
MqttConfig nextMqtt;
#endif
String escaped(const String& s) {
    String out;
    for (const char* p = s.c_str(); *p; ++p) {
        switch (*p) {
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += *p;
        }
    }
    return out;
}
}

WebServer::WebServer(uint16_t port) : AsyncWebServer(port) {
    on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
        r->send_P(200, "text/html", html_config_form, processor);
    });
    on("/save_config", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (pending) { r->send(409, "text/plain", "A configuration update is already pending"); return; }
        bool wifiChange = false, mqttChange = false, valid = true;
        String ssid = WiFi.SSID(), password = WiFi.psk();
#ifndef WIFI_SSID
        if (r->hasParam("wifi_ssid")) { ssid = r->getParam("wifi_ssid")->value(); wifiChange = true; }
        if (r->hasParam("wifi_password")) { password = r->getParam("wifi_password")->value(); wifiChange = true; }
        if (wifiChange && (!ssid.length() || ssid.length() > 32 || password.length() > 64)) valid = false;
#endif
#if defined(MQTT_ENABLED) && !defined(MQTT_IP)
        if (!g_mqtt) { r->send(503, "text/plain", "MQTT is not initialized yet"); return; }
        MqttConfig candidate(static_cast<const MqttConfig&>(*g_mqtt));
        if (r->hasParam("mqtt_ip")) {
            valid = candidate.setHost(r->getParam("mqtt_ip")->value().c_str()) && valid;
            mqttChange = true;
        }
        if (r->hasParam("mqtt_port")) {
            uint32_t p = 0;
            valid = Yokis::unsignedNumber(r->getParam("mqtt_port")->value().c_str(), 65535, p) && p > 0 && valid;
            if (valid) candidate.setPort(uint16_t(p));
            mqttChange = true;
        }
        if (r->hasParam("mqtt_username")) {
            valid = candidate.setUsername(r->getParam("mqtt_username")->value().c_str()) && valid;
            mqttChange = true;
        }
        if (r->hasParam("mqtt_password")) {
            valid = candidate.setPassword(r->getParam("mqtt_password")->value().c_str()) && valid;
            mqttChange = true;
        }
#endif
        if (!valid) { r->send(400, "text/plain", "Invalid configuration; nothing changed"); return; }
        if (!wifiChange && !mqttChange) { r->send(200, "text/plain", "No configuration changes"); return; }
        nextSsid = ssid; nextPassword = password;
#if defined(MQTT_ENABLED) && !defined(MQTT_IP)
        nextMqtt = candidate;
#endif
        pendingWifi = wifiChange; pendingMqtt = mqttChange;
        pendingAt = millis(); pending = true;
        // Do not reconnect WiFi/MQTT, write flash or yield inside AsyncTCP callbacks.
        r->send(202, "text/plain", "Configuration queued. Check the console for save/connection results.");
    });
}
WebServer::~WebServer() {}
void WebServer::processPending() {
    if (!pending || !Yokis::elapsed(millis(), pendingAt, 100)) return;
    bool ok = true;
#ifdef MQTT_ENABLED
    if (pendingMqtt) {
        ok = g_mqtt && g_mqtt->setConnectionInfo(nextMqtt);
        if (ok) g_mqtt->setDiscoveryDone(false);
    }
#endif
    if (ok && pendingWifi) setupWifi(nextSsid, nextPassword);
    LOG.println(ok ? "Configuration saved; connection will be retried" : "Configuration save failed; previous settings retained");
    pending = pendingWifi = pendingMqtt = false;
}
String WebServer::processor(const String& var) {
    if (var == "WIFI_SECTION_ENABLED") {
#ifdef WIFI_SSID
        return "disabled=\"\"";
#else
        return "";
#endif
    }
    if (var == "MQTT_SECTION_ENABLED") {
#if !defined(MQTT_ENABLED) || defined(MQTT_IP)
        return "disabled=\"\"";
#else
        return "";
#endif
    }
    if (var == "WIFI_SSID") return escaped(WiFi.SSID());
    if (var == "WIFI_PASSWORD") return escaped(WiFi.psk());
#ifdef MQTT_ENABLED
    if (!g_mqtt) return String();
    if (var == "MQTT_IP") return escaped(g_mqtt->getHost());
    if (var == "MQTT_PORT") return String(g_mqtt->getPort(), 10);
    if (var == "MQTT_USERNAME") return escaped(g_mqtt->getUsername());
    if (var == "MQTT_PASSWORD") return escaped(g_mqtt->getPassword());
#endif
    return String();
}
#endif

#ifdef ESP8266
#include "net/webserver.h"
#include "globals.h"
#include "reliability.h"
#include "buildIdentity.h"
#include "debugLevel.h"
#include "net/diagnostics.h"
#include "net/diagnosticPage.h"
#include <memory>
#include <new>
#include <osapi.h>

namespace {
// AsyncTCP callbacks validate and stage one update. Flash writes and network
// reconnects are deferred to loop(), avoiding reentrant work in the callback.
bool pending = false, pendingWifi = false, pendingMqtt = false;
uint32_t pendingAt = 0;
String nextSsid, nextPassword;
char csrfToken[33] = {};
#ifdef MQTT_ENABLED
MqttConfig nextMqtt;
#endif
String escaped(const String& s) {
    // Attribute escaping complements authentication; passwords are never rendered.
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
bool readPassword(AsyncWebServerRequest* r, const char* name, const char* clearName,
                  String& value, bool& changed) {
    const String replacement = r->hasParam(name, true) ? r->getParam(name, true)->value() : String();
    const bool clear = r->hasParam(clearName, true);
    if (clear && (!(r->getParam(clearName, true)->value() == "1") || replacement.length())) return false;
    if (clear) { value = ""; changed = true; }
    else if (replacement.length()) { value = replacement; changed = true; }
    // Blank/omitted fields preserve the existing secret. Clearing is explicit.
    return true;
}
}

WebServer::WebServer(uint16_t port, const char* username, const char* password)
    : AsyncWebServer(port), username_(username), password_(password) {
    on("/", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!authorize(r)) return;
        // No template processor: CSS percentages and data never become tokens.
        r->send_P(200, "text/html; charset=utf-8", diagnostic_html);
    });
    on("/config", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!authorize(r)) return;
        r->send_P(200, "text/html", html_config_form, processor);
    });
    on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (!authorize(r)) return;
        uint32_t page = 0;
        if (r->hasParam("page") &&
            !Yokis::unsignedNumber(r->getParam("page")->value().c_str(), 7, page)) {
            r->send(400, "text/plain", "Invalid page"); return;
        }
        size_t total = 0;
        for (auto* device : g_devices) if (device) ++total;
        if (page && page * 8 >= total) {
            r->send(400, "text/plain", "Invalid page"); return;
        }
        // A bounded heap buffer keeps the AsyncTCP callback's small stack free.
        // All pointers below stay alive until send() has copied the response.
        std::unique_ptr<char[]> json(new (std::nothrow) char[4096]);
        const String ssid = WiFi.SSID(), ip = WiFi.localIP().toString();
        Yokis::DiagnosticsSnapshot snapshot = {};
        snapshot.buildId = YOKIS_BUILD_ID;
        snapshot.gitRevision = YOKIS_BUILD_GIT_REVISION;
        snapshot.sourceSha256 = YOKIS_BUILD_SOURCE_SHA256;
        snapshot.ssid = ssid.c_str(); snapshot.ip = ip.c_str();
        snapshot.wifiConnected = WiFi.status() == WL_CONNECTED;
        snapshot.rssi = WiFi.RSSI(); snapshot.freeHeap = ESP.getFreeHeap();
        snapshot.uptimeMs = millis(); snapshot.debugLevel = Yokis::debugLevel(g_ConfigFlags);
#ifdef MQTT_ENABLED
        snapshot.mqttConfigured = g_mqtt && !g_mqtt->isEmpty();
        snapshot.mqttConnected = g_mqtt && g_mqtt->connected();
        snapshot.mqttHost = g_mqtt ? g_mqtt->getHost() : "";
        snapshot.mqttPort = g_mqtt ? g_mqtt->getPort() : 0;
#endif
        if (!json || !Yokis::writeDiagnosticJson(json.get(), 4096, snapshot,
                g_devices, MQTT_MAX_NUM_OF_YOKIS_DEVICES, page)) {
            r->send(503, "text/plain", "Diagnostics temporarily unavailable"); return;
        }
        r->send(200, "application/json; charset=utf-8", json.get());
    });
    on("/save_config", HTTP_GET, [this](AsyncWebServerRequest* r) {
        if (authorize(r)) r->send(405, "text/plain", "Use POST");
    });
    on("/save_config", HTTP_POST, [this](AsyncWebServerRequest* r) {
        if (!authorize(r)) return;
        if (!r->hasParam("csrf_token", true) || !(r->getParam("csrf_token", true)->value() == csrfToken)) {
            r->send(403, "text/plain", "Invalid form token"); return;
        }
        if (pending) { r->send(409, "text/plain", "A configuration update is already pending"); return; }
        bool wifiChange = false, mqttChange = false, valid = true;
        String ssid = WiFi.SSID(), password = WiFi.psk();
#ifndef WIFI_SSID
        if (r->hasParam("wifi_ssid", true)) { ssid = r->getParam("wifi_ssid", true)->value(); wifiChange = true; }
        valid = readPassword(r, "wifi_password", "wifi_password_clear", password, wifiChange) && valid;
        if (wifiChange && (!ssid.length() || ssid.length() > 32 || password.length() > 64)) valid = false;
#endif
#if defined(MQTT_ENABLED) && !defined(MQTT_IP)
        if (!g_mqtt) { r->send(503, "text/plain", "MQTT is not initialized yet"); return; }
        MqttConfig candidate(static_cast<const MqttConfig&>(*g_mqtt));
        if (r->hasParam("mqtt_ip", true)) {
            valid = candidate.setHost(r->getParam("mqtt_ip", true)->value().c_str()) && valid;
            mqttChange = true;
        }
        if (r->hasParam("mqtt_port", true)) {
            uint32_t p = 0;
            valid = Yokis::unsignedNumber(r->getParam("mqtt_port", true)->value().c_str(), 65535, p) && p > 0 && valid;
            if (valid) candidate.setPort(uint16_t(p));
            mqttChange = true;
        }
        if (r->hasParam("mqtt_username", true)) {
            valid = candidate.setUsername(r->getParam("mqtt_username", true)->value().c_str()) && valid;
            mqttChange = true;
        }
        String mqttPassword(candidate.getPassword());
        bool passwordChange = false;
        valid = readPassword(r, "mqtt_password", "mqtt_password_clear", mqttPassword, passwordChange) && valid;
        if (passwordChange) { valid = candidate.setPassword(mqttPassword.c_str()) && valid; mqttChange = true; }
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
bool WebServer::authorize(AsyncWebServerRequest* r) const {
    if (!username_ || !*username_ || !Yokis::validAdminSecret(password_) || !csrfToken[0]) {
        r->send(503, "text/plain", "Web administration is disabled"); return false;
    }
    if (!r->authenticate(username_, password_)) { r->requestAuthentication(); return false; }
    return true;
}
void WebServer::begin() {
    if (!username_ || !*username_ || !Yokis::validAdminSecret(password_)) return;
    if (!csrfToken[0]) {
        unsigned char entropy[16];
        if (os_get_random(entropy, sizeof(entropy)) != 0) return;
        const char hex[] = "0123456789abcdef";
        for (size_t i = 0; i < sizeof(entropy); ++i) {
            csrfToken[2*i] = hex[entropy[i] >> 4]; csrfToken[2*i+1] = hex[entropy[i] & 15];
        }
        csrfToken[32] = 0;
    }
    AsyncWebServer::begin();
}
WebServer::~WebServer() {}
void WebServer::processPending() {
    // The short delay gives the HTTP response a chance to leave before Wi-Fi
    // is reconfigured. HTTP 202 acknowledges queuing, not successful saving.
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
    nextSsid = nextPassword = String();
#ifdef MQTT_ENABLED
    nextMqtt.setPassword("");
#endif
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
    if (var == "CSRF_TOKEN") return csrfToken;
    if (var == "WIFI_PASSWORD" || var == "MQTT_PASSWORD") return String();
#ifdef MQTT_ENABLED
    if (!g_mqtt) return String();
    if (var == "MQTT_IP") return escaped(g_mqtt->getHost());
    if (var == "MQTT_PORT") return String(g_mqtt->getPort(), 10);
    if (var == "MQTT_USERNAME") return escaped(g_mqtt->getUsername());
#endif
    return String();
}
#endif

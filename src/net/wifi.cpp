#ifdef ESP8266
#include "net/wifi.h"
#include "globals.h"

static void prepareStation() {
    // Reconnection must not rewrite or erase credentials saved in SDK flash.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoReconnect(true);
}

static void disconnectMqtt() {
#ifdef MQTT_ENABLED
    if (g_mqtt) g_mqtt->disconnect();
#endif
}

void setupWifi(String ssid, String password) {
    if (strcmp(WiFi.SSID().c_str(), ssid.c_str()) == 0 &&
        strcmp(WiFi.psk().c_str(), password.c_str()) == 0) {
        // Core 3.x may have matching credentials with its radio still OFF.
        reconnectWifi();
        return;
    }
    disconnectMqtt();
    prepareStation();
    // Only an explicit credential change writes persistent station settings.
    // Do not erase the old credentials before saving their replacement.
    WiFi.persistent(true);
    WiFi.setAutoConnect(true);
    WiFi.begin(ssid.c_str(), password.c_str(), 0, NULL, true);
    WiFi.persistent(false);
}

int reconnectWifi() {
    WiFi.persistent(false);
    if (WiFi.status() == WL_CONNECTED && (WiFi.getMode() & WIFI_STA)) {
        WiFi.setAutoReconnect(true);
        return WL_CONNECTED;
    }
    disconnectMqtt();
    prepareStation();
    WiFi.reconnect();
    LOG.print("Connecting to WiFi ");
    for (uint8_t c = 0; c < 100; ++c) {
        if (WiFi.status() == WL_CONNECTED) {
            LOG.print(" connected to SSID "); LOG.println(WiFi.SSID());
            return WL_CONNECTED;
        }
        delay(50);
    }
    LOG.println(" unavailable; saved credentials retained, automatic reconnect enabled");
    return WiFi.status();
}

void setupWifi() {
    if (reconnectWifi() == WL_CONNECTED) return;
    if (WiFi.SSID().length()) return; // missing AP is NOT an instruction to erase config
    setupWifiAP();
}

void setupWifiAP() {
    disconnectMqtt();
    WiFi.persistent(false);
    WiFi.disconnect(false, false); // keep station credentials in RAM and SDK flash
    WiFi.mode(WIFI_AP);
    String ssid = "YokisHack-";
    ssid += String(ESP.getFlashChipId(), HEX);
    WiFi.softAP(ssid, "");
    LOG.print("WiFi AP mode started. SSID: "); LOG.println(ssid);
    LOG.print("YokisHack IP: "); LOG.println(WiFi.softAPIP());
}

bool resetWifiConfig() {
    disconnectMqtt();
    // This function is only called by the explicit wifiReset command.
    WiFi.persistent(true);
    const bool ok = WiFi.disconnect(true, true);
    WiFi.persistent(false);
    return ok && WiFi.SSID().length() == 0;
}
#endif

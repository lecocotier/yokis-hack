#include <Arduino.h>
#include "reliability.h"

#include "RF/copy.h"
#include "RF/e2bp.h"
#include "RF/irqManager.h"
#include "RF/pairing.h"
#include "RF/scanner.h"
#include "commands/callbacks.h"
#include "globals.h"
#include "printf.h"
#include "serial/serialHelper.h"

#ifdef ESP8266
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFiUdp.h>
#include "net/mqttHass.h"
#include "net/wifi.h"
#include "net/webserver.h"
#endif

// static irqType initialization
volatile IrqType IrqManager::irqType = PAIRING;

// globals' initialization
byte g_ConfigFlags = FLAG_POLLING;
SerialHelper* g_serial;
Pairing* g_pairingRF;
E2bp* g_bp;
Scanner* g_scanner;
Copy* g_copy;
Device* g_currentDevice;

#ifdef ESP8266
#ifdef MQTT_ENABLED
MqttHass* g_mqtt;
#endif // MQTT_ENABLED
TelnetSpy g_telnetAndSerial;
// no need to store more devices than supported by MQTT
Device* g_devices[MQTT_MAX_NUM_OF_YOKIS_DEVICES];
#endif // ESP8266

//
#ifdef ESP8266

WebServer webserver(80);
Ticker* g_deviceStatusPollers[MQTT_MAX_NUM_OF_YOKIS_DEVICES];
WiFiClient espClient;

// polling
void pollForStatus(Device* device);

#ifdef MQTT_ENABLED
void mqttCallback(char*, uint8_t*, unsigned int);
#endif // MQTT_ENABLED

#endif // ESP8266

// Setup inits everything: singletons and commands' callback
void setup() {
    randomSeed(micros());

    // Globals' initialization
    g_serial = new SerialHelper();
    g_pairingRF = new Pairing(CE_PIN, CSN_PIN);
    g_bp = new E2bp(CE_PIN, CSN_PIN);
    g_scanner = new Scanner(CE_PIN, CSN_PIN);
    g_copy = new Copy(CE_PIN, CSN_PIN);
    g_currentDevice = new Device(CURRENT_DEVICE_DEFAULT_NAME);

#ifdef ESP8266
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, HIGH);  // pin is inverted so, set it off

    // Load all previously stored devices from LittleFS memory
    reloadConfig(NULL);

    // Setting up configured wifi or AP mode
    // If compilation options are present, override any existing configuration
    #ifdef WIFI_SSID
        String ssid = WIFI_SSID;
        String psk = "";
        #ifdef WIFI_PASSWORD
        psk = WIFI_PASSWORD;
        #endif // WIFI_PASSWORD
        LOG.print("WIFI_SSID is set, forcing this configuration. SSID=");
        LOG.println(WIFI_SSID);
        setupWifi(ssid, psk);
    #else
        setupWifi(); // Setup existing configuration of set AP mode for initial config
    #endif // WIFI_SSID

    // Starting webserver
    webserver.begin();

    #if defined(MQTT_ENABLED)
    g_mqtt = new MqttHass(espClient);
    g_mqtt->setCallback(mqttCallback);
    #endif

    // OTA
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else {  // U_FS
            type = "filesystem";
        }
        LOG.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() { LOG.println("\nEnd"); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        LOG.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        LOG.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            LOG.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            LOG.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            LOG.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            LOG.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            LOG.println("End Failed");
        }
    });
    ArduinoOTA.begin();
#endif

    // Registering all commands
    registerAllCallbacks();

    // Handle interrupt pin
    pinMode(IRQ_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(IRQ_PIN), IrqManager::processIRQ, FALLING);

    printf_begin();  // Works only for Arduino devices...
    LOG.println("Setup finished - device ready !");
    g_serial->executeCallback("help");
    LOG.println();
    g_serial->prompt();
}

void loop() {
#if defined(ESP8266)
    LOG.handle(); // telnetspy handling
    webserver.processPending();
    ArduinoOTA.handle();

    #if defined(MQTT_ENABLED)
    g_mqtt->loop();

    if (g_mqtt->connected() && !g_mqtt->isDiscoveryDone()) {
        bool complete = true;
        for (uint8_t i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i) {
            if (g_devices[i] && (!g_mqtt->publishDevice(g_devices[i]) ||
                                !g_mqtt->subscribeDevice(g_devices[i]))) {
                complete = false; break;
            }
        }
        g_mqtt->setDiscoveryDone(complete);
    } else if (g_mqtt->connected() && FLAG_IS_ENABLED(FLAG_POLLING)) {
        // One poll per main-loop iteration: queued commands get serviced
        // between devices instead of waiting through up to 64 timeouts.
        static uint8_t next = 0;
        for (uint8_t n = 0; n < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++n) {
            uint8_t i = next;
            next = (next + 1) % MQTT_MAX_NUM_OF_YOKIS_DEVICES;
            if (g_devices[i] && g_devices[i]->needsPolling()) {
                pollForStatus(g_devices[i]); break;
            }
        }
    }
#endif // MQTT_ENABLED
#endif // ESP8266
    if (IrqManager::irqType == SCANNER) g_scanner->service();
    g_serial->readFromSerial();
    delay(1);
}


#if defined(ESP8266) && defined(MQTT_ENABLED)
void pollForStatus(Device* d) {
    if (!d || d->getMode() == NO_RCPT) return;
    IrqManager::irqType = E2BP;
    g_bp->setDevice(d);
    DeviceStatus ds = g_bp->pollForStatus();
    
    if (g_bp->hasResponse()) {  // reachability is separate from decoding
        if (d->getFailedPollings() > 0) {
            LOG.print("Device ");
            LOG.print(d->getName());
            LOG.println(" recovered");
        }

        d->pollingSuccess();

        if (d->isOffline()) {  // Device is back online
            d->online();
            g_mqtt->notifyOnline(d);
        }

        // Update device status - even if unchanged
        // Hence, in case of hass restart, status are updated
        d->setStatus(ds);
        if (d->getMode() == DIMMER) {
            if (ds == ON && d->getBrightness() == 0)
                d->setBrightness(BRIGHTNESS_MAX);
            g_mqtt->notifyBrightness(d);
        } else {
            g_mqtt->notifyPower(d);
        }
    } else {
        if (d->pollingFailed() >= DEVICE_MAX_FAILED_POLLING_BEFORE_OFFLINE) {
            // Device is unreachable
            LOG.print("Device ");
            LOG.print(d->getName());
            LOG.println(" is offline");
            d->offline();
            g_mqtt->notifyOffline(d);
        } else {
            LOG.print("Failed to check device ");
            LOG.print(d->getName());
            LOG.print(" ");
            LOG.print(d->getFailedPollings(), DEC);
            LOG.print("/");
            LOG.println(DEVICE_MAX_FAILED_POLLING_BEFORE_OFFLINE, DEC);
        }
    }
}
#endif

#if defined(ESP8266) && defined(MQTT_ENABLED)
void mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    Yokis::MqttRequest request;
    if (!Yokis::parseMqtt(topic, payload, length, request)) {
        LOG.println("MQTT command rejected: invalid topic or payload"); return;
    }
    Device* d = Device::getFromList(g_devices, MQTT_MAX_NUM_OF_YOKIS_DEVICES, request.device);
    if (!d || !d->isConfigured() || (request.brightness && d->getMode() != DIMMER) ||
        (request.command == Yokis::Stop && d->getMode() != SHUTTER)) {
        LOG.println("MQTT command rejected: unknown device or unsupported command"); return;
    }
    if (d->getMode() != SHUTTER && d->isDuplicateCommand(request.command, millis())) {
        LOG.println("MQTT duplicate of the same acknowledged command ignored"); return;
    }
    IrqManager::irqType = E2BP;
    g_bp->setDevice(d);
    bool ok = false;
    switch (request.command) {
        case Yokis::PowerOn: ok = g_bp->on(); break;
        case Yokis::PowerOff: case Yokis::DimmerOff: ok = g_bp->off(); break;
        case Yokis::Stop: ok = g_bp->pauseShutter(); break;
        case Yokis::DimmerMin: ok = g_bp->dimmerMin(); break;
        case Yokis::DimmerMid: ok = g_bp->dimmerMid(); break;
        case Yokis::DimmerMax: ok = g_bp->dimmerMax(); break;
        default: return;
    }
    LOG.print("RF command "); LOG.print(d->getName());
    LOG.print(ok ? " completed; " : " unconfirmed; ");
    LOG.print("response="); LOG.print(g_bp->hasResponse() ? "yes" : "no");
    LOG.print(" cycles="); LOG.println(g_bp->getTxCycles());
    if (ok) {
        // Only a command updates this history. Polls do not; failures do not.
        if (g_bp->hasResponse()) d->acknowledgeCommand(request.command, millis());
        if (g_bp->hasResponse() && d->isOffline()) { d->online(); g_mqtt->notifyOnline(d); }
    } else {
        d->setStatus(UNDEFINED); // never publish the intended motion as proven
    }
    if (d->getMode() == DIMMER) {
        if (ok) g_mqtt->notifyBrightness(d);
        else g_mqtt->notifyPower(d, UNDEFINED);
    } else g_mqtt->notifyPower(d);
}
#endif  // ESP8266 && MQTT_ENABLED

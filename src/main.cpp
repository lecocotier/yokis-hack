#include <Arduino.h>
#include "reliability.h"
#include "postStopPolling.h"

#include "RF/copy.h"
#include "RF/e2bp.h"
#include "RF/irqManager.h"
#include "RF/pairing.h"
#include "RF/scanner.h"
#include "commands/callbacks.h"
#include "globals.h"
#include "printf.h"
#include "serial/serialHelper.h"

// This module owns startup and cooperative scheduling. Radio helpers execute
// synchronously: priority checks below prevent starting background work ahead
// of an already queued command, but cannot preempt a transaction in progress.
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

// Before normal operation, route an unexpected IRQ to the pairing helper.
volatile IrqType IrqManager::irqType = PAIRING;

// Application-owned objects live for the firmware lifetime. E2bp, Scanner and
// Copy borrow Device pointers; reloadConfig() invalidates those before deletion.
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
AdminConsole g_telnetAndSerial;
// no need to store more devices than supported by MQTT
Device* g_devices[MQTT_MAX_NUM_OF_YOKIS_DEVICES];
#endif // ESP8266

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

// Load persistent devices before networking so discovery starts from one
// coherent configuration. Static global pointers are initially null, including
// g_mqtt while the first reloadConfig() and Wi-Fi setup are running.
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

    // Explicit build-time credentials take precedence over SDK-saved settings.
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
        setupWifi(); // Reuse saved credentials; use AP mode for initial setup.
    #endif // WIFI_SSID

    // Starting webserver
    webserver.begin();

    #if defined(MQTT_ENABLED)
    g_mqtt = new MqttHass(espClient);
    g_mqtt->setCallback(mqttCallback);
    #endif

    // Report a deliberate gateway shutdown before OTA disconnects networking.
    // Authentication is configured below from the ignored local secret header.
    ArduinoOTA.onStart([]() {
        if (g_bp && g_bp->holdActive()) { g_bp->cancelHold(); g_bp->serviceHold(); }
#ifdef MQTT_ENABLED
        if (g_mqtt) g_mqtt->disconnect();
#endif
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
        LOG.printf("Progress: %u%%\r", total ? unsigned((uint64_t(progress) * 100) / total) : 0);
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
    if (!Yokis::configureAdminOta(ArduinoOTA, YOKIS_OTA_PASSWORD))
        LOG.println("OTA disabled: provision a local OTA password first");
#endif

    // Registering all commands
    registerAllCallbacks();

    // Attach only after every IRQ target exists. IRAM safety also depends on
    // the callees inside RF24_forked, not just this handler's IRAM_ATTR.
    pinMode(IRQ_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(IRQ_PIN), IrqManager::processIRQ, FALLING);

    printf_begin();  // Works only for Arduino devices...
    LOG.println("Setup finished - device ready !");
    g_serial->executeCallback("help");
    LOG.println();
    g_serial->prompt();
}

void loop() {
    IrqManager::servicePending();
    bool automaticRadioWork = false;
#if defined(ESP8266)
    LOG.handle(); // authenticate and assemble complete USB/network command lines
    webserver.processPending();
    ArduinoOTA.handle();

    #if defined(MQTT_ENABLED)
    // Expiration changes metadata only. A late STOP check must not manufacture
    // a position, nor start radio traffic while a special RF mode owns the chip.
    expirePostStopChecks(); // deadline failure is metadata, never automatic RF
    g_mqtt->loop();

    if (!g_mqtt->handledInput() && !g_mqtt->hasPendingInput() && !consoleInputPending() &&
        !postStopChecksPending()) g_mqtt->serviceRefresh(g_devices, MQTT_MAX_NUM_OF_YOKIS_DEVICES);
    // Drain already received commands before ANY automatic status query.
    // No recursive MQTT calls from the radio; special RF modes retain ownership.
    if (FLAG_IS_ENABLED(FLAG_POLLING) && IrqManager::irqType == E2BP &&
        !g_mqtt->handledInput() && !g_mqtt->hasPendingInput() && !consoleInputPending()) {
        automaticRadioWork = servicePostStopPolling();
        if (!automaticRadioWork && !postStopChecksPending() && !g_bp->holdActive() &&
            g_mqtt->connected() && g_mqtt->isDiscoveryDone()) {
        // One poll per main-loop iteration: queued commands get serviced
        // between devices instead of waiting through up to 64 timeouts.
        static uint8_t next = 0;
        for (uint8_t n = 0; n < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++n) {
            uint8_t i = next;
            next = (next + 1) % MQTT_MAX_NUM_OF_YOKIS_DEVICES;
            if (g_devices[i] && g_devices[i]->needsPolling() &&
                !g_devices[i]->shutterFeedback().verification().pending()) {
                pollForStatus(g_devices[i]); automaticRadioWork = true; break;
            }
        }
        }
    }
#endif // MQTT_ENABLED
#endif // ESP8266
    if (IrqManager::irqType == SCANNER) g_scanner->service();
    g_serial->readFromSerial();
    bool canServiceHold = !automaticRadioWork && IrqManager::irqType == E2BP && !consoleInputPending();
#if defined(ESP8266) && defined(MQTT_ENABLED)
    canServiceHold = canServiceHold && !g_mqtt->handledInput() &&
        !g_mqtt->hasPendingInput() && !postStopChecksPending();
#endif
    if (canServiceHold) {
        Device* changed = g_bp->serviceHold();
#if defined(ESP8266) && defined(MQTT_ENABLED)
        if (changed) {
            if (changed->getStatus() == UNDEFINED) g_mqtt->notifyPower(changed, UNDEFINED);
            else g_mqtt->notifyBrightness(changed);
        }
#else
        (void)changed;
#endif
    }
    delay(1);
}


#if defined(ESP8266) && defined(MQTT_ENABLED)
void pollForStatus(Device* d) {
    if (!d || d->getMode() == NO_RCPT) return;
    IrqManager::irqType = E2BP;
    g_bp->setDevice(d);
    const bool checking = d->getMode() == SHUTTER && d->shutterFeedback().verification().pending();
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

        // Refresh the observation timestamp even for an unchanged state. A
        // received but undecodable reply proves reachability, not position.
        d->setStatus(ds);
        if (d->getMode() == DIMMER) {
            if (ds == ON && d->getBrightness() == 0)
                d->setBrightness(BRIGHTNESS_MAX);
            g_mqtt->notifyBrightness(d);
        } else {
            g_mqtt->notifyPower(d);
        }
    } else if (checking) {
        // Fast verification retries are not three independent periodic failures:
        // don't declare a reachable device Offline within a fraction of a second.
        d->clearPollingRequest();
        d->setStatus(ds);
        g_mqtt->notifyPower(d);
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
    // PubSubClient lends both buffers for this callback only. Validate and copy
    // the bounded device name before touching the shared radio helper.
    if (g_mqtt && g_mqtt->handleBirth(topic, payload, length)) return;
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

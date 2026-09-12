#include "commands/callbacks.h"

#include "globals.h"
#include "reliability.h"

void registerAllCallbacks() {
    // Serial setup
    g_serial->registerCallback(new GenericCallback(
        "pair",
        "Pair with a Yokis device - basically act as "
        "if a Yokis remote is in pairing mode (5 button clicks)",
        pairingCallback));
    g_serial->registerCallback(
        new GenericCallback("toggle",
                            "Toggle a device; shutter toggle does not send a cancelling release",
                            toggleCallback));
    g_serial->registerCallback(
        new GenericCallback("scan",
                            "Scan the network for packets - polling has to be "
                            "disabled for this to work",
                            scannerCallback));
    g_serial->registerCallback(new GenericCallback(
        "copy",
        "Copy a device to a pairing one (or disconnect if already configured)",
        copyCallback));
    g_serial->registerCallback(new GenericCallback(
        "dConfig", "display loaded config / current config", displayDevices));
    g_serial->registerCallback(new GenericCallback(
        "on", "Switch ON the configured device", onCallback));
    g_serial->registerCallback(new GenericCallback(
        "off", "Switch OFF the configured device", offCallback));
    g_serial->registerCallback(new GenericCallback(
        "pause", "Pause the configured device (MVR500 only - shutter device)",
        pauseShutterCallback));
    g_serial->registerCallback(new GenericCallback(
        "press", "Press and hold an e2bp button", pressCallback));
    g_serial->registerCallback(new GenericCallback(
        "pressFor", "Press and hold for x milliseconds", pressForCallback));
    g_serial->registerCallback(new GenericCallback(
        "release", "Release an e2bp button", releaseCallback));
    g_serial->registerCallback(
        new GenericCallback("status", "Get device status", statusCallback));
    g_serial->registerCallback(new GenericCallback(
        "dimmem", "Set a dimmer to memory (= 1 button pushes)",
        dimmerMemCallback));
    g_serial->registerCallback(new GenericCallback(
        "dimmax", "Set a dimmer to maximum (= 2 button pushes)",
        dimmerMaxCallback));
    g_serial->registerCallback(new GenericCallback(
        "dimmid", "Set a dimmer to middle (= 3 button pushes)",
        dimmerMidCallback));
    g_serial->registerCallback(new GenericCallback(
        "dimmin", "Set a dimmer to minimum (= 4 button pushes)",
        dimmerMinCallback));
    g_serial->registerCallback(new GenericCallback(
        "dimnil", "Set a dimmer to night light mode (= 7 button pushes)",
        dimmerNilCallback));

#ifdef ESP8266
    g_serial->registerCallback(new GenericCallback(
        "save", "Save current device configuration to LittleFS",
        storeConfigCallback));
    g_serial->registerCallback(new GenericCallback(
        "delete", "Delete one entry from LittleFS configuration",
        deleteFromConfig));
    g_serial->registerCallback(new GenericCallback(
        "clear", "Clear all config previously stored to LittleFS",
        clearConfig));
    g_serial->registerCallback(new GenericCallback(
        "reload", "Reload config from LittleFS to memory", reloadConfig));
    g_serial->registerCallback(new GenericCallback(
        "dConfigFS", "display config previously stored in LittleFS",
        displayConfig));
    g_serial->registerCallback(new GenericCallback(
        "dRestore",
        "restore a previously saved raw config line (SPIFFS->LittleFS)",
        restoreConfig));
    g_serial->registerCallback(
        new GenericCallback("wifiConfig",
                            "Configure WiFi: ssid [psk]; quote arguments containing spaces",
                            wifiConfig));
    g_serial->registerCallback(new GenericCallback(
        "wifiReconnect", "Reconnect using saved WiFi credentials, without erasing them", wifiReconnect));
    g_serial->registerCallback(new GenericCallback(
        "wifiDiag", "Display wifi configuration debug info", wifiDiag));
    g_serial->registerCallback(new GenericCallback(
        "wifiReset", "Reset wifi configuration and setup AP mode",
        resetWifiConfigCallback));
    g_serial->registerCallback(
        new GenericCallback("restart", "Restart the ESP8266 board", restart));

#ifdef MQTT_ENABLED
    g_serial->registerCallback(
        new GenericCallback("mqttConfig",
                            "Configure MQTT options (format: mqttConfig host "
                            "port username password)",
                            mqttConfig));
    g_serial->registerCallback(new GenericCallback(
        "mqttDiag", "Display current MQTT configuration", mqttDiag));
    g_serial->registerCallback(new GenericCallback(
        "mqttConfigDelete", "Delete current MQTT configuration",
        mqttConfigDelete));
#endif // MQTT_ENABLED

#endif // ESP8266
}

bool pairingCallback(const char*) {
    uint8_t buf[5];  // enough size for addr, serial and version
    IrqManager::irqType = PAIRING;
    bool res = g_pairingRF->hackPairing();
    if (res) {
        Device fresh(CURRENT_DEVICE_DEFAULT_NAME);
        g_currentDevice->copy(&fresh);
        g_pairingRF->getAddressFromRecvData(buf);
        g_currentDevice->setHardwareAddress(buf);
        g_currentDevice->setChannel(g_pairingRF->getChannelFromRecvData());

        g_pairingRF->getSerialFromRecvData(buf);
        g_currentDevice->setSerial(buf);

        g_pairingRF->getVersionFromRecvData(buf);
        g_currentDevice->setVersion(buf);

        // Get device status and get device mode
        statusCallback(NULL);
        LOG.println("Product type is not detectable from a status byte. Use save <name> SHUTTER (or ON_OFF/DIMMER/NO_RCPT).");

        g_currentDevice->toSerial();
    }
    return res;
}

// Get a device from the list with the given params
Device* getDeviceFromParams(const char* params) {
#ifdef ESP8266
    Yokis::Arguments args(params);
    if (!args.valid) return NULL;
    if (args.count < 2) return g_currentDevice;
    return Device::getFromList(g_devices, MQTT_MAX_NUM_OF_YOKIS_DEVICES, args.at(1));
#else
    (void)params; return g_currentDevice;
#endif
}

// Generic on, off or toggle
bool changeDeviceState(const char* params, bool (E2bp::*func)(void)) {
    Device* d = getDeviceFromParams(params);

    if (d == NULL || !d->isConfigured()) {
        LOG.println("No such device");
        return false;
    }

    IrqManager::irqType = E2BP;
    g_bp->setDevice(d);
    bool ret = (g_bp->*func)();
#if defined(ESP8266) && defined(MQTT_ENABLED)
    if (ret) {
        if (d->getMode() == DIMMER) {
            g_mqtt->notifyBrightness(d);
        } else {
            g_mqtt->notifyPower(d);
        }
    }
#endif
    return ret;
}

bool toggleCallback(const char* params) {
    return changeDeviceState(params, &E2bp::toggle);
}

bool onCallback(const char* params) {
    return changeDeviceState(params, &E2bp::on);
}

bool offCallback(const char* params) {
    return changeDeviceState(params, &E2bp::off);
}

bool pauseShutterCallback(const char* params) {
    return changeDeviceState(params, &E2bp::pauseShutter);
}

bool scannerCallback(const char* params) {
    if (FLAG_IS_ENABLED(FLAG_POLLING)) {
        LOG.println("Disable polling before attempting to scan ! Aborting.");
        return false;
    }

    Device* d = getDeviceFromParams(params);

    if (d == NULL || !d->isConfigured()) {
        LOG.println("No such device");
        return false;
    }

    IrqManager::irqType = SCANNER;
    g_scanner->setDevice(d);
    g_scanner->setupRFModule();

    return true;
}

bool copyCallback(const char* params) {
    Device* d = getDeviceFromParams(params);

    if (d == NULL || !d->isConfigured()) {
        LOG.println("No such device");
        return false;
    }

    IrqManager::irqType = COPYING;
    g_copy->setDevice(d);
    return g_copy->send();
}

bool displayDevices(const char*) {
#ifdef ESP8266
    for (unsigned i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i)
        if (g_devices[i]) g_devices[i]->toSerial();
#else
    g_currentDevice->toSerial();
#endif
    return true;
}

bool dimmerMemCallback(const char* params) { return changeDeviceState(params, &E2bp::dimmerMem); }
bool dimmerMaxCallback(const char* params) { return changeDeviceState(params, &E2bp::dimmerMax); }
bool dimmerMidCallback(const char* params) { return changeDeviceState(params, &E2bp::dimmerMid); }
bool dimmerMinCallback(const char* params) { return changeDeviceState(params, &E2bp::dimmerMin); }
bool dimmerNilCallback(const char* params) { return changeDeviceState(params, &E2bp::dimmerNiL); }

bool pressCallback(const char* params) {
    Device* d = getDeviceFromParams(params);

    if (d == NULL || !d->isConfigured()) {
        LOG.println("No such device");
        return false;
    }

    IrqManager::irqType = E2BP;
    g_bp->setDevice(d);
    g_bp->reset();
    g_bp->setupRFModule();
    bool ret = g_bp->press();
    return ret;
}

bool pressForCallback(const char* params) {
    Yokis::Arguments args(params); uint32_t duration;
    if (!args.valid || args.count != 3 || !Yokis::unsignedNumber(args.at(2), 600000, duration) || duration <= 700) {
        LOG.println("Usage: pressFor <device> <duration_ms>, 701..600000"); return false;
    }
    Device* d = getDeviceFromParams(params);
    if (!d || !d->isConfigured()) return false;
    IrqManager::irqType = E2BP; g_bp->setDevice(d);
    return g_bp->pressAndHoldFor(duration);
}

bool releaseCallback(const char* params) {
    Device* d = getDeviceFromParams(params);

    if (d == NULL || !d->isConfigured()) {
        LOG.println("No such device");
        return false;
    }

    IrqManager::irqType = E2BP;
    g_bp->setDevice(d);
    g_bp->reset();
    g_bp->setupRFModule();
    bool ret = g_bp->release();
    return ret;
}

bool statusCallback(const char* params) {
    Device* d = getDeviceFromParams(params);
    if (!d || !d->isConfigured()) { LOG.println("No such configured device"); return false; }
    IrqManager::irqType = E2BP; g_bp->setDevice(d);
    DeviceStatus st = g_bp->pollForStatus();
    LOG.print("Device Status = "); LOG.println(Device::getStatusAsString(st));
    if (!g_bp->hasResponse()) LOG.println("No radio response");
    else if (st == UNDEFINED) LOG.println("Radio response received, state not decoded");
    return g_bp->hasResponse();
}

#ifdef ESP8266
bool storeConfigCallback(const char* params) {
    Yokis::Arguments args(params);
    if (!args.valid || args.count < 2 || args.count > 3 || !Yokis::validDeviceName(args.at(1))) {
        LOG.println("Usage: save <name> [ON_OFF|DIMMER|SHUTTER|NO_RCPT], name max 48 characters"); return false;
    }
    if (args.count == 3 && strcmp(args.at(2), "ON_OFF") && strcmp(args.at(2), "DIMMER") &&
        strcmp(args.at(2), "SHUTTER") && strcmp(args.at(2), "NO_RCPT")) return false;
    Device saved(g_currentDevice); saved.setName(args.at(1));
    if (args.count == 3) saved.setMode(args.at(2));
    if (!saved.saveToLittleFS()) return false;
    LOG.println("Saved."); return reloadConfig(NULL);
}

bool clearConfig(const char*) {
    return Device::clearConfigFromLittleFS() && reloadConfig(NULL);
}

bool displayConfig(const char*) {
    Device::displayConfigFromLittleFS();
    return true;
}

bool restoreConfig(const char* params) {
    Yokis::Arguments args(params);
    return args.valid && args.count == 2 && Device::storeRawConfig(args.at(1)) && reloadConfig(NULL);
}

bool reloadConfig(const char*) {
    Device* loaded[MQTT_MAX_NUM_OF_YOKIS_DEVICES] = {};
    int n = Device::loadFromLittleFS(loaded, MQTT_MAX_NUM_OF_YOKIS_DEVICES);
    if (n < 0) return false;
    // Detach all callbacks BEFORE freeing their Device arguments.
    for (unsigned i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i)
        if (g_deviceStatusPollers[i]) g_deviceStatusPollers[i]->detach();
    if (g_bp) g_bp->setDevice(NULL);
    if (g_scanner) { g_scanner->ce(LOW); g_scanner->setDevice(NULL); }
    if (g_copy) g_copy->setDevice(NULL);
    IrqManager::irqType = E2BP;
#ifdef MQTT_ENABLED
    if (g_mqtt) {
        for (unsigned i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i) {
            if (g_devices[i]) {
                Device* next = Device::getFromList(loaded, n, g_devices[i]->getName());
                if (!next || next->getMode() != g_devices[i]->getMode()) g_mqtt->removeDiscovery(g_devices[i]);
            }
        }
        g_mqtt->clearSubscriptions(); g_mqtt->setDiscoveryDone(false);
    }
#endif
    for (unsigned i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i) {
        delete g_deviceStatusPollers[i]; g_deviceStatusPollers[i] = NULL;
        delete g_devices[i]; g_devices[i] = loaded[i];
        if (g_devices[i] && g_devices[i]->getMode() != NO_RCPT) {
            g_deviceStatusPollers[i] = new Ticker();
            if (g_deviceStatusPollers[i])
                g_deviceStatusPollers[i]->attach_ms(random(4000, 10000), pollDevice, g_devices[i]);
        }
    }
    LOG.println("Reloaded."); return true;
}

bool deleteFromConfig(const char* params) {
    Yokis::Arguments args(params);
    return args.valid && args.count == 2 && Device::deleteFromConfig(args.at(1)) && reloadConfig(NULL);
}

// Interrupt function
void pollDevice(Device* d) {
    if (FLAG_IS_ENABLED(FLAG_POLLING)) d->pollMePlease();
}

bool resetWifiConfigCallback(const char* params) {
    resetWifiConfig();
    return restart(NULL);
}

bool wifiConfig(const char* params) {
    Yokis::Arguments args(params);
    if (!args.valid || args.count < 2 || args.count > 3 || !*args.at(1) ||
        strlen(args.at(1)) > 32 || strlen(args.at(2)) > 64) {
        LOG.println("Usage: wifiConfig <ssid> [password], quoted arguments supported"); return false;
    }
    setupWifi(args.at(1), args.at(2)); return true;
}

bool wifiReconnect(const char*) { return reconnectWifi() == WL_CONNECTED; }

bool wifiDiag(const char* params) {
    WiFi.printDiag(LOG);
    LOG.print("Yokis-Hack IP: ");
    if (WiFi.getMode() == WIFI_AP) {
        LOG.println(WiFi.softAPIP());
    } else {
        LOG.println(WiFi.localIP());
    }

    return true;
}

bool restart(const char* params) {
#ifdef MQTT_ENABLED
    if (g_mqtt) g_mqtt->disconnect();
#endif
    ESP.restart();
    return true;
}

#if defined(MQTT_ENABLED)
bool mqttConfig(const char* params) {
    Yokis::Arguments args(params); uint32_t port;
    if (!args.valid || args.count < 3 || args.count > 5 || !*args.at(1) ||
        !Yokis::unsignedNumber(args.at(2), 65535, port) || !port) {
        LOG.println("Usage: mqttConfig <host> <port 1..65535> [user] [password]"); return false;
    }
    bool ok = g_mqtt->setConnectionInfo(args.at(1), uint16_t(port), args.at(3), args.at(4));
    if (ok) g_mqtt->setDiscoveryDone(false);
    return ok;
}

bool mqttDiag(const char* params) {
    LOG.println("Current MQTT configuration:");
    g_mqtt->printDebug(LOG);
    LOG.print("Client ID: "); LOG.println(g_mqtt->clientId());
    LOG.print("Gateway availability topic: "); LOG.println(g_mqtt->gatewayTopic());
    LOG.print("HA refresh pending: "); LOG.println(g_mqtt->refreshPending());
    return true;
}

bool mqttConfigDelete(const char*) {
    if (!MqttConfig::deleteConfigFromLittleFS()) return false;
    MqttConfig emptyConfig;
    if (!g_mqtt->setConnectionInfo(emptyConfig, false)) return false;
    g_mqtt->setDiscoveryDone(false);
    LOG.println("MQTT config deleted!"); return true;
}

#endif // MQTT_ENABLED
#endif // ESP8266

#pragma once
#include <Arduino.h>
#include "RF/device.h"
#include "postStopPolling.h"
#include "RF/e2bp.h"
#include "RF/pairing.h"
#include "RF/copy.h"
#include "RF/scanner.h"
#include "RF/irqManager.h"
#include "net/mqttHass.h"
#include "serial/serialHelper.h"
#include <Ticker.h>
#define LOG Serial
#define SERIAL_BAUDRATE 115200
#define CURRENT_DEVICE_DEFAULT_NAME "tempDevice"
#define PROG_TITLE_FORMAT "Yokis %s"
#define PROG_VERSION "test"
#define YOKIS_CMD_BEGIN 0x35
#define YOKIS_CMD_END 0x53
#define YOKIS_CMD_STATUS 0
#define YOKIS_CMD_ON 0xb9
#define YOKIS_CMD_OFF 0x1a
#define YOKIS_CMD_SHUTTER_OFF 0xfa
#define YOKIS_CMD_SHUTTER_PAUSE 0x1a
#define DEVICE_MAX_FAILED_POLLING_BEFORE_OFFLINE 3
#define MQTT_UPDATE_MILLIS_WINDOW 100
#define FLAG_DEBUG 1
#define FLAG_RAW 2
#define FLAG_POLLING 4
#define FLAG_IS_ENABLED(f) ((g_ConfigFlags & (f))!=0)
#define IS_DEBUG_ENABLED FLAG_IS_ENABLED(FLAG_DEBUG)
#define FLAG_ENABLE(f) (g_ConfigFlags|=(f))
#define FLAG_DISABLE(f) (g_ConfigFlags&=~(f))
#define FLAG_TOGGLE(f) (g_ConfigFlags^=(f))
extern byte g_ConfigFlags;
extern Device* g_devices[MQTT_MAX_NUM_OF_YOKIS_DEVICES];
extern Ticker* g_deviceStatusPollers[MQTT_MAX_NUM_OF_YOKIS_DEVICES];
extern E2bp* g_bp;
extern Pairing* g_pairingRF;
extern Scanner* g_scanner;
extern Copy* g_copy;
extern Device* g_currentDevice;
extern MqttHass* g_mqtt;
extern SerialHelper* g_serial;
#include "net/webserver.h"
struct FakeOTA{void handle(){}};
extern FakeOTA ArduinoOTA;
extern WebServer webserver;
void loop();
void mqttCallback(char*,uint8_t*,unsigned int);
void pollForStatus(Device*);

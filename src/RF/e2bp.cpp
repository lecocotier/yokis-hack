#include "RF/e2bp.h"
#include "globals.h"
#include "utils.h"
#include "reliability.h"

// E2bp implements the captured Yokis command exchange over the low-level NRF
// driver. Payload bytes and TX/RX timing are installation-specific evidence;
// they must not be replaced by upstream defaults without radio measurements.
E2bp::E2bp(uint16_t cepin, uint16_t cspin) : RFConfigurator(cepin, cspin), device(NULL) {
    radioReady = false; reset();
}
// Device is borrowed from the configured list, never owned by this radio helper.
E2bp::~E2bp() {}
void E2bp::reset() {
    firstPayloadStatus = secondPayloadStatus = UNDEFINED;
    loopContinue = true; receivedResponse = false; txCycles = 0;
    memset(answerBuf, 0, sizeof(answerBuf));
}
void E2bp::setDevice(Device* d) {
    device = d; radioReady = false; reset();
}

const Device* E2bp::getDevice() { return this->device; }

// On/off replies describe the resulting state, while a dimmer can report its
// pre-command state. Keep both replies so callers can apply that distinction.
DeviceStatus E2bp::getLastKnownDeviceStatus() {
    if (secondPayloadStatus == UNDEFINED) {
        // We probably never received the second payload.
        // This happens in certain RF environments...
        return firstPayloadStatus;
    }

    // Prefer the second decoded reply when available; dimmer callers still
    // account for the protocol's pre-command state semantics.
    return secondPayloadStatus;
}

bool E2bp::setDeviceStatus(DeviceStatus ds) {
    uint8_t buf[PAYLOAD_LENGTH];

    if (!device || !device->isConfigured()) return false;
    reset();
    // Shutters have distinct directional and STOP payloads. Their command
    // response may predate movement, so ShutterFeedback owns the estimate.
    if(device->getMode() == SHUTTER ) {
        if (ds == SHUTTER_STOPPED) {
            getPayload(buf, PL_SHUTTERPAUSE);
        } else if (ds == SHUTTER_OPENING) {
            getPayload(buf, PL_SHUTTEROPENING);
        } else if (ds == SHUTTER_CLOSING) {
            getPayload(buf, PL_SHUTTERCLOSING);
        } else {
            return false;
        }
    } else {
        getPayload(buf, ds == ON ? PL_ON : PL_OFF);
    }

    bool ret = false;
    const uint32_t start = millis();
    if (device->getMode() == SHUTTER) {
        device->shutterFeedback().begin(ds == SHUTTER_STOPPED ? Yokis::ShutterFeedback::Pause :
            ds == SHUTTER_OPENING ? Yokis::ShutterFeedback::Up : Yokis::ShutterFeedback::Down, start);
    }
    setupRFModule();
    if (!radioReady) {
        if (device->getMode() == SHUTTER)
            device->setStatus(device->shutterFeedback().finish(false, millis()));
        return false;
    }
    do { ret = sendPayload(buf); } while (!ret && device->getMode() != NO_RCPT && !Yokis::elapsed(millis(), start, 1000));

    if (device->getMode() == SHUTTER) {
        device->setStatus(device->shutterFeedback().finish(ret, millis()));
    } else if (ret || device->getMode() == NO_RCPT) {  // if NO_RCPT, ignore ret
        device->setStatus(ds);
        if (device->getMode() == DIMMER) {
            if (ds == ON)
                device->setBrightness(BRIGHTNESS_MAX);
            else
                device->setBrightness(BRIGHTNESS_OFF);
        }
    }

    return ret || device->getMode() == NO_RCPT;
}

bool E2bp::on() { 
    if (!device) return false;
    if(device->getMode() == SHUTTER ) {
        return setDeviceStatus(SHUTTER_OPENING);
    } else {
        return setDeviceStatus(ON); 
    }
}

bool E2bp::off() { 
    if (!device) return false;
    if(device->getMode() == SHUTTER ) {
        return setDeviceStatus(SHUTTER_CLOSING);
    } else {
        return setDeviceStatus(OFF); 
    }
}

bool E2bp::pauseShutter() {
    return device && device->getMode() == SHUTTER && setDeviceStatus(SHUTTER_STOPPED);
}

// Emulate a remote-button sequence. Direct ON/OFF commands require fewer
// exchanges; a shutter toggle deliberately omits the cancelling release.
bool E2bp::toggle() {
    if (!device || !device->isConfigured()) return false;
    const uint32_t start = millis();
    bool retPress = false, retRelease = false;

    reset();
    if (device->getMode() == SHUTTER)
        device->shutterFeedback().begin(Yokis::ShutterFeedback::Toggle, millis());
    setupRFModule();
    if (!radioReady) {
        if (device->getMode() == SHUTTER)
            device->setStatus(device->shutterFeedback().finish(false, millis()));
        return false;
    }
    if (device->getMode() == SHUTTER) {
        // On the captured shutter protocol, 0x53 after 0x35 stops movement.
        // Send one toggle, not a repeated press/release train that cancels it.
        bool ok = press();
        device->setStatus(device->shutterFeedback().finish(ok, millis()));
        return ok;
    }
    while (!Yokis::elapsed(millis(), start, 1000)) {
        retPress = press();
        if (device->getMode() == DIMMER)
            delay(100);  // No need to wait for on/off devices

        if (retPress) {  // only do it if press has been ack (true)
            retRelease = release();

            if (device->getMode() == ON_OFF) {
                if (firstPayloadStatus != secondPayloadStatus &&
                    secondPayloadStatus != UNDEFINED)
                    break;  // Status changed successfully
            } else if (device->getMode() == DIMMER) {
                break;  // assuming it has been toggled ok
            }
        }
    }

    // Preserve a useful state even if the release reply is missing: the press
    // can have acted physically already. The return value below still requires
    // both replies, so a partial exchange is not reported as fully confirmed.
    if (retPress && device->getMode() == ON_OFF) {
        // we know the status, set it
        device->setStatus(getLastKnownDeviceStatus());
    } else if (retPress && device->getMode() == DIMMER) {
        // dimmer replies with the current status BEFORE applying the command
        // so need to invert it
        device->setStatus(getLastKnownDeviceStatus());
        device->toggleStatus();
    }

    return retPress && retRelease;
}

// See Yokis MTV500ER manual for those configs
// Note: depending on configuration, 2 pulses can set to memory or 100%
// default is 100% for 2 pulses, that's what we will be using here...
bool E2bp::dimmerMem() { return dimmerSet(1); }
bool E2bp::dimmerMax() { return dimmerSet(2); }
bool E2bp::dimmerMid() { return dimmerSet(3); }
bool E2bp::dimmerMin() { return dimmerSet(4); }
bool E2bp::dimmerNiL() { return dimmerSet(7); }
bool E2bp::dimmerSet(const uint8_t number) {
    if (!device || device->getMode() != DIMMER) {
        LOG.println("Not a dimmer device, ignoring.");
        return false;
    }

    bool ret = true;

    for (uint8_t i = 0; i < number && ret; i++) {
        ret = toggle();
        delay(10);
    }

    // Brightness is inferred from the accepted pulse sequence, not measured.
    // With the assumed dimmer configuration: 2=max, 3=mid, 4=min. A single
    // pulse is the toggle already handled above; it is not a second toggle.
    if (number > 1 && ret)
        device->setStatus(ON);
    // toggle() has already updated the state for a single pulse.

    if (ret) {
        switch (number) {
            case 0:
                device->setBrightness(BRIGHTNESS_OFF);
                break;
            case 2:
                device->setBrightness(BRIGHTNESS_MAX);
                break;
            case 3:
                device->setBrightness(BRIGHTNESS_MID);
                break;
            case 4:
                device->setBrightness(BRIGHTNESS_MIN);
                break;
        }
    }

    return ret;
}

// Ask a device to give its status
DeviceStatus E2bp::pollForStatus() {
    // Using 0 for 'begin' packet seems to do the trick
    uint8_t buf[PAYLOAD_LENGTH];
    reset();
    if (!device || !device->isConfigured()) return UNDEFINED;
    const bool checking = device->getMode() == SHUTTER &&
        device->shutterFeedback().beginVerificationPoll(millis());
    setupRFModule();
    if (radioReady) {
        getPayload(buf, PL_STATUS);
        sendPayload(buf);
    }
    if (checking)
        firstPayloadStatus = device->shutterFeedback().finishVerificationPoll(receivedResponse, millis());
    return firstPayloadStatus;
}

// A status reply cannot reliably identify the device model. Preserve the
// user's configured type instead of guessing from overlapping reply bytes.
DeviceMode E2bp::getDeviceModeFromRecvData() {
    // A status byte is not a product identifier. Retain the configured type.
    return device ? device->getMode() : ON_OFF;
}

bool E2bp::press() { return press(false); }

bool E2bp::press(bool dim) {
    if (!device || !device->isConfigured()) return false;
    const bool ownContext = device->getMode() == SHUTTER && !device->shutterFeedback().pending();
    if (ownContext) device->shutterFeedback().begin(Yokis::ShutterFeedback::Toggle, millis());
    if (!radioReady) {
        if (ownContext) device->setStatus(device->shutterFeedback().finish(false, millis()));
        return false;
    }
    secondPayloadStatus = UNDEFINED;
    bool ret = true;
    if (IS_DEBUG_ENABLED) LOG.println("Button pressing");
    uint8_t buf[PAYLOAD_LENGTH];

    firstPayloadStatus = UNDEFINED;
    if (dim)
        getPayload(buf, PL_DIM);
    else
        getPayload(buf, PL_BEGIN);
    ret = sendPayload(buf);

    if (ownContext) device->setStatus(device->shutterFeedback().finish(ret, millis()));
    if (IS_DEBUG_ENABLED) LOG.println("Button pressed");
    return ret;
}

bool E2bp::release() {
    if (!device || !device->isConfigured() || !radioReady) return false;
    bool ret = true;
    if (IS_DEBUG_ENABLED) LOG.println("Button releasing");
    uint8_t buf[PAYLOAD_LENGTH];

    getPayload(buf, PL_END);
    ret = sendPayload(buf);

    if (IS_DEBUG_ENABLED) LOG.println("Button released");
    if (ret && device->getMode() == ON_OFF) {
        device->setStatus(getLastKnownDeviceStatus());
    } else if (ret && device->getMode() == DIMMER) {
        device->setStatus(getLastKnownDeviceStatus());
        device->toggleStatus();
    }
    return ret;
}

// Press and hold a button
// Send begin payload and dimming payload while
// release() has not been called
bool E2bp::pressAndHoldFor(unsigned long duration) {
    // Only for dimmers and must be > 700ms
    if (!device || device->getMode() != DIMMER || duration <= 700 || duration > 600000) return false;

    reset();
    setupRFModule();
    if (!radioReady || !press()) return false;

    const uint32_t holdStart = millis();
    uint32_t startTime = holdStart;
    // Every second until timeout, send a dim command
    do {
        // send payload every 1 second
        if (millis() - startTime >= 1000) {
            startTime = millis();
            reset();
            setupRFModule();
            press(true);
        }
        yield();
        delayMicroseconds(1000);  // 1 ms delay
    } while (!Yokis::elapsed(millis(), holdStart, uint32_t(duration)));

    bool ret = release();
    // no brightness level available but device is ON
    if (ret) device->setBrightness(BRIGHTNESS_MAX);
    return ret;
}

// Individual shutter orders retain 0x06 and a zero fourth byte. Upstream also
// supports bus-wide commands (0x16) and different address fields; those are
// intentionally not inferred from an individual device's configuration here.
uint8_t* E2bp::getPayload(uint8_t* buf, PayloadType type) {
    buf[0] = 0x00;
    buf[1] = device->getMode() == SHUTTER ? 0x06 : 0x04;
    buf[2] = 0x00;
    buf[3] = 0x00;
    buf[4] = 0x00;
    buf[5] = 0x00;
    buf[6] = random(0, 0xff);
    buf[7] = 0x00;
    buf[8] = 0x00;

    switch (type) {
        case PL_BEGIN:
            buf[0] = YOKIS_CMD_BEGIN;
            break;
        case PL_END:
            buf[0] = YOKIS_CMD_END;
            break;
        case PL_ON:
            buf[0] = YOKIS_CMD_ON;
            break;
        case PL_OFF:
            buf[0] = device->getMode() == SHUTTER ? YOKIS_CMD_SHUTTER_OFF : YOKIS_CMD_OFF;
            break;
        case PL_STATUS:
            buf[0] = 0;
            buf[1] = 0;
            break;
        case PL_DIM:
            buf[0] = YOKIS_CMD_BEGIN;
            buf[7] = 0x02;
            break;
        case PL_SHUTTERPAUSE:
            buf[0] = YOKIS_CMD_SHUTTER_PAUSE;
            break;
        case PL_SHUTTEROPENING:
            buf[0] = YOKIS_CMD_ON;
            break;
        case PL_SHUTTERCLOSING:
            buf[0] = YOKIS_CMD_SHUTTER_OFF;
            break;        
    }

    return buf;
}

bool E2bp::sendPayload(const uint8_t* payload) {
    if (!device || !device->isConfigured() || !radioReady || !payload) return false;
    if (IS_DEBUG_ENABLED) {
        LOG.print("Payload: ");
        for (uint8_t i = 0; i < 9; i++) {
            LOG.print(payload[i], HEX);
            LOG.print(" ");
        }
        LOG.println();
    }

    setupPayload(payload);
    return runMainLoop();
}

void E2bp::setupRFModule() {
    radioReady = device && device->isConfigured() && begin();
    if (!radioReady) { LOG.println("RF initialization failed or device not configured"); return; }
    // Keep the captured register sequence explicit. Replacing it with a
    // higher-level helper may add reads, delays or other register changes.
    write_register(RF_CH, device->getChannel());

    write_register(RF_SETUP, 0b00100011);

    write_register(RX_ADDR_P0, device->getHardwareAddress(), 5);

    write_register(TX_ADDR, device->getHardwareAddress(), 5);

    write_register(RX_PW_P0, 0x02);

    write_register(EN_RXADDR, 1);
    write_register(EN_AA, 0);

    write_register(NRF_STATUS, 0b01110000);
    write_register(SETUP_RETR, 0);

    write_register(NRF_CONFIG, 0b00001110);

    yield();
    delayMicroseconds(4000);  // Settling delay retained from the SPI capture.
    flush_rx();               // done when calling begin() but anyway...
    if (IS_DEBUG_ENABLED) {
        printDetails();
    }
}

bool E2bp::runMainLoop() {
    // A response ends this bounded exchange. IRQ edges are only a wake-up
    // hint: the FIFO must contain a real two-byte reply before reporting RX.
    const uint32_t start = millis();
    receivedResponse = false;
    uint8_t nbLoops = 0;

    ce(LOW);
    // while not interrupted by RX, timeout or by device mode
    while (loopContinue && !Yokis::elapsed(millis(), start, MAIN_LOOP_TIMEOUT_MILLIS)) {
        if (available()) break; // handle data even if an IRQ edge was missed
        if (txCycles < 65535) ++txCycles;
        write_register(NRF_CONFIG, 0b00001110);  // PTX
        ce(HIGH);
        yield();
        delayMicroseconds(15);
        ce(LOW);
        yield();
        delayMicroseconds(685);

        write_register(NRF_CONFIG, 0b00001111);  // PRX
        ce(HIGH);
        write_register(NRF_STATUS, 0b01110000);  // Reset interrupts
        spiTrans(REUSE_TX_PL);
        yield();
        delayMicroseconds(1000);
        ce(LOW);

        if (device->getMode() == NO_RCPT && (nbLoops++) >= 30) {
            break;  // Stop sending, we are not gonna receive anything
        }
    }

    // We got here right after being interrupted
    if (available()) {
        setPayloadSize(sizeof(answerBuf)); // RX is two bytes, TX is nine
        read(answerBuf, sizeof(answerBuf));
        receivedResponse = true;

        // These transaction traces are currently unconditional. They are not
        // ISR logging; their cost still contributes to synchronous RF latency.
        LOG.print("Received: ");
        LOG.print(device->getName());
        LOG.print(" (");
        LOG.print(device->getModeAsString(device->getMode()));
        LOG.print(") ");
        printBinaryRepresentation(answerBuf[0], true);
        LOG.print(" ");
        printBinaryRepresentation(answerBuf[1], true);
        LOG.print(" - ");

        // The same resting bytes can mean an endpoint or an intermediate STOP.
        // ShutterFeedback combines raw observations with this device's command
        // history; MqttHass publishes the origin/uncertainty of every estimate.
        if (device->getMode() == SHUTTER) {
            Yokis::ShutterFeedback& feedback = device->shutterFeedback();
            firstPayloadStatus = feedback.observe(answerBuf[0], answerBuf[1], millis());
            LOG.print(feedback.pending() ? "Command reply (pre-state possible)" :
                      Device::getStatusAsString(firstPayloadStatus));
            LOG.print(" - Source: "); LOG.print(feedback.pending() ? "command_reply" : feedback.sourceName());
            LOG.print(" - Time: "); LOG.print(millis());
            LOG.print(" - Last cmd: "); LOG.print(feedback.commandName());
            LOG.print(" - Cmd time: "); LOG.print(feedback.commandAt());
            LOG.print(" - Delta: ");
            if (feedback.hasCommand()) LOG.print(uint32_t(millis() - feedback.commandAt()));
            else LOG.print("n/a");
            LOG.print(" - Previous: "); LOG.println(Device::getStatusAsString(device->getStatus()));
        } else {
            if (firstPayloadStatus == UNDEFINED) {
                if (answerBuf[1] == 1 || answerBuf[0] == 0x2f) {
                    firstPayloadStatus = ON;
                    LOG.println("On");
                } else {
                    firstPayloadStatus = OFF;
                    LOG.println("Off");
                }
            } else {
                if (answerBuf[1] == 1 || answerBuf[0] == 0x2f) {
                    secondPayloadStatus = ON;
                    LOG.println("On");
                } else {
                    secondPayloadStatus = OFF;
                    LOG.println("Off");
                }
            }
        }
    }

    ce(LOW);
    flush_tx(); // stop reuse before a different operation takes the radio
    write_register(NRF_STATUS, 0b01110000);
    return receivedResponse; // an interrupt alone is not proof of a payload
}

void E2bp::setupPayload(const uint8_t* payload) {
    // Prepare everything to send continuously the given payload
    ce(LOW);
    flush_rx(); // do not accept a stale response from the previous transaction
    write_register(NRF_STATUS, 0b01110000);
    loopContinue = true;
    setPayloadSize(PAYLOAD_LENGTH);
    flush_tx();
    write_payload(payload, PAYLOAD_LENGTH, W_TX_PAYLOAD);
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void E2bp::stopMainLoop() {
    loopContinue = false;
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void E2bp::interruptTxOk() {
    // The immediate TX->RX switch is retained from the captured protocol.
    // No logging here. Its SPI callees still require IRAM/reentrancy hardening;
    // adding IRAM_ATTR to this function alone does not make the path safe.
    write_register(NRF_CONFIG, 0b00001111);  // PRX
    ce(HIGH);
    write_register(NRF_STATUS, 0b01110000);  // Reset interrupts
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void E2bp::interruptRxReady() {
    // Defer FIFO reads and interpretation to runMainLoop().
    stopMainLoop();
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void E2bp::interruptTxFailed() {
    // Ignore
    // No logging from the ISR. The caller reports transaction failure.
}

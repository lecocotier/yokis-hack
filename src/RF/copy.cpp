#include "RF/copy.h"
#include "reliability.h"

Copy::Copy(uint16_t cepin, uint16_t cspin, Device* device)
    : Pairing(cepin, cspin) {
    setDevice(device);
    reset();
}

Copy::Copy(uint16_t cepin, uint16_t cspin) : Copy(cepin, cspin, NULL) {}

void Copy::setDevice(Device* device) { this->device = device; }

void Copy::setupRFModule() {
    setCRCLength(RF24_CRC_16);
    setPALevel(RF24_PA_LOW);
    setChannel(PAIRING_CHANNEL_NUMBER);
    setAutoAck(true);
    setAddressWidth(sizeof(Pairing::pairingAddress));
    delay(5);
}

bool Copy::send() {
    if (!device || !device->isConfigured()) return false;

    const uint32_t start = millis();
    isFailed = false;

    if (!begin()) return false;
    setupRFModule();
    maskIRQ(true, true, true); // write() polls STATUS; ISR must not clear it

    // First payload
    //
    openWritingPipe(pairingAddress);
    stopListening();

    memcpy(recvBuffer, device->getVersion(), FIRST_PAYLOAD_SIZE);
    _debugPrintRecv(recvBuffer, FIRST_PAYLOAD_SIZE);
    setPayloadSize(FIRST_PAYLOAD_SIZE);
    bool firstOk = write(recvBuffer, FIRST_PAYLOAD_SIZE);

    memcpy(recvBuffer, device->getHardwareAddress(), 2);
    recvBuffer[2] = device->getChannel();
    memcpy(recvBuffer + 3, device->getSerial(), 2);
    _debugPrintRecv(recvBuffer, SECOND_PAYLOAD_SIZE);
    setPayloadSize(SECOND_PAYLOAD_SIZE);
    bool secondOk = firstOk && write(recvBuffer, SECOND_PAYLOAD_SIZE);

    while (!Yokis::elapsed(millis(), start, 200)) {
        delay(1);  // let time to send packets
    }

    /*
    LOG.print(device->getName());
    if (isFailed) {
        LOG.println(" - Copy failed.");
    } else {
        LOG.println(" - Copied successfully.");
    }*/

    return firstOk && secondOk;
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void Copy::interruptRxReady() {
    // Nothing will be received
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void Copy::interruptTxOk() { isFailed = false; }

#if defined(ESP8266)
IRAM_ATTR
#endif
void Copy::interruptTxFailed() {
    isFailed = true;
}

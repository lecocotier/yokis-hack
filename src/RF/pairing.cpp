#include "RF/pairing.h"
#include <Arduino.h>
#include "globals.h"
#include "reliability.h"

// Constants' declaration
const byte Pairing::pairingAddress[] = {0xbe, 0xbe, 0xbe, 0xbe, 0xbe};

// Functions
Pairing::Pairing(uint16_t cepin, uint16_t cspin)
    : RFConfigurator(cepin, cspin) {
    reset();
}

void Pairing::reset() {
    this->recvBufferAddr = this->recvBuffer;
    memset(this->recvBuffer, 0, 8);
    this->readsCount = 0;

}

bool Pairing::hackPairing() {
    reset();
    uint32_t phaseStart = millis();
    bool firstReceived = false;

    if (!FLAG_IS_ENABLED(FLAG_RAW) || FLAG_IS_ENABLED(FLAG_DEBUG)) {
        LOG.println("Hack started, click on the connect button when ready");
    }

    if (!begin()) return false;
    setupRFModule();

    if (FLAG_IS_ENABLED(FLAG_DEBUG)) {
        printDetails();
    }

    LOG.print("Waiting... timeout=");
    LOG.println(HACK_TIMEOUT);

    while (readsCount < 2) {
        if (readsCount && !firstReceived) { firstReceived = true; phaseStart = millis(); }
        if (Yokis::elapsed(millis(), phaseStart, firstReceived ? 1000 : HACK_TIMEOUT)) break;
        delay(10);
    }
    ce(LOW);

    if (readsCount >= 2) {
        _debugPrintRecv(recvBuffer, 3);
        _debugPrintRecv(recvBuffer + 3, 5);
        printPairingInfo();
        return true;
    }

    LOG.println("Timeout waiting for data, aborting.");
    return false;
}

void Pairing::setupRFModule() {
    // This has been sniffed from SPI
    setCRCLength(RF24_CRC_16);
    setPALevel(RF24_PA_LOW);
    setChannel(PAIRING_CHANNEL_NUMBER);
    setAutoAck(true);
    setAddressWidth(sizeof(Pairing::pairingAddress));
    prepareForReading(3);
    delay(5);
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void Pairing::prepareForReading(uint8_t payloadSize) {
    setPayloadSize(payloadSize);
    openReadingPipe(PAIRING_PIPE_NUM, Pairing::pairingAddress);
    startListening();
}

#if defined(ESP8266)
IRAM_ATTR
#endif
void Pairing::interruptTxOk() {}
#if defined(ESP8266)
IRAM_ATTR
#endif
void Pairing::interruptRxReady() {
    if (readsCount >= 2 || !available()) return;
    uint8_t count = readsCount;
    read(recvBuffer + (count ? 3 : 0), count ? 5 : 3);
    if (!count) prepareForReading(5); // timing-critical, no logging here
    else ce(LOW);
    readsCount = count + 1; // publish only after the bytes are stored
}
#if defined(ESP8266)
IRAM_ATTR
#endif
void Pairing::interruptTxFailed() {}
void Pairing::_debugPrintRecv(byte* recvBuf, uint8_t size) {
    if (!IS_DEBUG_ENABLED) return;
    LOG.print("Buffer data: ");
    for (uint8_t i = 0; i < size; ++i) { LOG.print(recvBuf[i], HEX); LOG.print(" "); }
    LOG.println();
}

// Get address on which communication occur with the device after successful
// pairing
void Pairing::getAddressFromRecvData(uint8_t buf[5]) {
    buf[0] = recvBuffer[3];
    buf[1] = recvBuffer[4];
    buf[2] = recvBuffer[3];
    buf[3] = recvBuffer[4];
    buf[4] = recvBuffer[4];
}

// Get channel on which communication occur with the device after successful
// pairing
byte Pairing::getChannelFromRecvData() { return recvBuffer[5]; }

// Get device's version bytes - not sure what those bytes are for yet
void Pairing::getVersionFromRecvData(uint8_t buf[3]) {
    memcpy(buf, recvBuffer, 3);
}

// Get device's serial - not sure what those bytes are for yet
void Pairing::getSerialFromRecvData(uint8_t buf[2]) {
    memcpy(buf, recvBuffer+sizeof(uint8_t)*6, 2);
}

// Print pairing information to configured serial
void Pairing::printPairingInfo() {
    if (FLAG_IS_ENABLED(FLAG_RAW))
        _printPairingInfoRaw();
    else
        _printPairingInfoFormat();
}

void Pairing::_printPairingInfoRaw() {
    char buf[32];
    byte addr[5];
    byte channel = getChannelFromRecvData();
    getAddressFromRecvData(addr);

    snprintf(buf, sizeof(buf), "address=%02x%02x%02x%02x%02x,channel=%02x", addr[0], addr[1],
            addr[2], addr[3], addr[4], channel);
    LOG.println(buf);
}

void Pairing::_printPairingInfoFormat() {
    char buf[32];
    byte addr[5];
    byte channel = getChannelFromRecvData();
    getAddressFromRecvData(addr);

    LOG.println("Here are the info got from the device:");

    LOG.print("  Address: ");
    snprintf(buf, sizeof(buf), "%02x %02x %02x %02x %02x", addr[0], addr[1], addr[2], addr[3],
            addr[4]);
    LOG.println(buf);

    LOG.print("  Channel: ");
    LOG.println(channel, HEX);
}

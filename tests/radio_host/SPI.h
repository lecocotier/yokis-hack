#pragma once
#include "radioTestHardware.h"
#define SPI_HAS_TRANSACTION 1
#define MSBFIRST 1
#define SPI_MODE0 0
class SPISettings { public: SPISettings(unsigned, unsigned, unsigned) {} };
class TestSPI { public:
 void begin() {} void beginTransaction(SPISettings) {radioTestBoundary(true);} void endTransaction() {radioTestBoundary(false);}
 uint8_t transfer(uint8_t byte) { return radioTestTransfer(byte); }
};
extern TestSPI SPI;

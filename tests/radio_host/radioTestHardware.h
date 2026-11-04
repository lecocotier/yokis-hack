#pragma once
#include <stdint.h>
uint32_t radioTestDisable();
void radioTestRestore(uint32_t);
uint8_t radioTestTransfer(uint8_t);
bool radioTestIrqTransfer(uint8_t, uint8_t&);
void radioTestCsn(uint16_t, bool);
void radioTestCe(uint16_t, bool);
void radioTestDelay(unsigned);
void radioTestBoundary(bool);
void radioTestDigitalWrite(int, int);

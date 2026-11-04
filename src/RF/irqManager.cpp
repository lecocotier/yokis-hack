#include "RF/irqManager.h"
#include "RF/configurator.h"
#include "globals.h"
#include "RF/radioCritical.h"

// One physical NRF is shared by all helpers. irqType selects the current
// owner; callbacks must not log, allocate or yield in interrupt context.
// Explicit qualified dispatch avoids both a flash switch table and virtual
// calls through flash-resident vtables when the instruction cache is disabled.
#if defined(ESP8266)
IRAM_ATTR
#endif
void IrqManager::processIRQ() {
    const IrqType owner = irqType;
#define DISPATCH_IRQ(Type, pointer) do { \
    Type* rf = (pointer); \
    if (!rf) return; \
    const uint8_t status = rf->irqStatusAndClear(); \
    if (status & 0x20) rf->Type::interruptTxOk(); \
    if (status & 0x10) rf->Type::interruptTxFailed(); \
    if (status & 0x40) rf->Type::interruptRxReady(); \
} while (false)
    if (owner == PAIRING) { DISPATCH_IRQ(Pairing, g_pairingRF); }
    else if (owner == E2BP) { DISPATCH_IRQ(E2bp, g_bp); }
    else if (owner == SCANNER) { DISPATCH_IRQ(Scanner, g_scanner); }
    else if (owner == COPYING) { DISPATCH_IRQ(Copy, g_copy); }
#undef DISPATCH_IRQ
}

void IrqManager::servicePending() {
    if (digitalRead(IRQ_PIN) != LOW) return;
    Yokis::RadioCritical guard;
    if (digitalRead(IRQ_PIN) == LOW) processIRQ();
}

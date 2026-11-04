#include "RF/irqManager.h"
#include "RF/configurator.h"
#include "globals.h"

// One physical NRF is shared by all helpers. irqType selects the current
// owner; callbacks must not log, allocate or yield in interrupt context.
// IRAM_ATTR covers this entry point only: the compiled RF24_forked callees
// still reside in flash in the audited build (see doc/audit-2026-11-04.md).
#if defined(ESP8266)
IRAM_ATTR
#endif
void IrqManager::processIRQ() {
    // All helpers address the same chip, but their IRQ side effects differ.
    RFConfigurator* rf = NULL;
    bool txOk, txFailed, rxReady;

    switch (IrqManager::irqType) {
        case PAIRING:
            rf = g_pairingRF;
            break;
        case E2BP:
            rf = g_bp;
            break;
        case SCANNER:
            rf = g_scanner;
            break;
        case COPYING:
            rf = g_copy;
            break;
    }

    if (!rf) return;
    rf->whatHappened(txOk, txFailed, rxReady);
    if (txOk)     rf->interruptTxOk();
    if (txFailed) rf->interruptTxFailed();
    if (rxReady)  rf->interruptRxReady();
}

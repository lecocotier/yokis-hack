#ifndef __IRQ_MANAGER_H__
#define __IRQ_MANAGER_H__

// Enum to define what kinds of IRQ interrupt we get at our disposal. This
// specify what RF object we use for the callbacks TX_DS, RX_DR and RT_MAX (see
// STATUS (0x07) register from the datasheet)
enum IrqType { PAIRING, E2BP, SCANNER, COPYING };

// IrqManager manages IRQ interrupt raised by NRF chip's IRQ pin
class IrqManager {
   public:
    static volatile IrqType irqType;
    static void processIRQ();
    // Foreground recovery for an IRQ still held low after a missed edge or a
    // bounded ISR bus timeout. Never call from inside a CSN transaction.
    static void servicePending();
};

#endif  // __IRQ_MANAGER_H__

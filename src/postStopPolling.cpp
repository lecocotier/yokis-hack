#if defined(ESP8266) && defined(MQTT_ENABLED)
#include "globals.h"
#include "postStopPolling.h"
void pollForStatus(Device* device);

void expirePostStopChecks() {
    for (unsigned i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i) {
        Device* d = g_devices[i];
        if (!d || d->getMode() != SHUTTER) continue;
        if (d->shutterFeedback().expireVerification(millis())) {
            d->setStatus(UNDEFINED);
            d->clearPollingRequest();
            LOG.print("Post-STOP "); LOG.print(d->getName());
            LOG.println(" result=inconclusive reason=deadline");
            if (g_mqtt) g_mqtt->notifyPower(d);
        }
    }
}

bool postStopChecksPending() {
    for (unsigned i = 0; i < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++i)
        if (g_devices[i] && g_devices[i]->getMode() == SHUTTER &&
            g_devices[i]->shutterFeedback().verification().pending()) return true;
    return false;
}

bool servicePostStopPolling() {
    // Poll OFF / special RF modes suppress ALL automatic RF, including STOP checks.
    if (!FLAG_IS_ENABLED(FLAG_POLLING) || IrqManager::irqType != E2BP) return false;
    static unsigned next = 0;
    for (unsigned n = 0; n < MQTT_MAX_NUM_OF_YOKIS_DEVICES; ++n) {
        unsigned i = next;
        next = (next + 1) % MQTT_MAX_NUM_OF_YOKIS_DEVICES;
        Device* d = g_devices[i];
        if (!d || d->getMode() != SHUTTER || !d->isConfigured()) continue;
        const Yokis::StopVerification& check = d->shutterFeedback().verification();
        if (!check.due(millis())) continue;
        LOG.print("Post-STOP "); LOG.print(d->getName());
        LOG.print(" attempt="); LOG.print(check.attempts() + 1);
        LOG.print("/3 age_ms="); LOG.println(check.age(millis()));
        pollForStatus(d);
        LOG.print("Post-STOP "); LOG.print(d->getName());
        LOG.print(" result="); LOG.print(check.name());
        LOG.print(" response="); LOG.print(g_bp->hasResponse() ? "yes" : "no");
        LOG.print(" state="); LOG.println(Device::getStatusAsString(d->getStatus()));
        return true; // one radio transaction, then service commands again
    }
    return false;
}
#endif

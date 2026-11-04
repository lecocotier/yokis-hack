#include "statusRefresh.h"
#if defined(ESP8266) && defined(MQTT_ENABLED)
#include "globals.h"
void pollForStatus(Device*);
#endif
namespace Yokis {
namespace { bool pending=false; unsigned next=0; }
void requestStatusRefresh() { next=0;pending=true; }
void cancelStatusRefresh() { pending=false;next=0; }
bool serviceStatusRefresh() {
#if defined(ESP8266) && defined(MQTT_ENABLED)
    // The caller grants radio ownership only after draining commands/STOPs.
    // Store an index, never a Device pointer which reload could invalidate.
    while (pending && next<MQTT_MAX_NUM_OF_YOKIS_DEVICES) {
        Device* d=g_devices[next++];
        if (!d || !d->isConfigured() || d->getMode()==NO_RCPT || d->getMode()==SHUTTER_BUS) continue;
        pollForStatus(d);
        if (next==MQTT_MAX_NUM_OF_YOKIS_DEVICES) pending=false;
        return true;
    }
#endif
    pending=false;
    return false;
}
}

#include "RF/bus.h"
#if defined(ESP8266) && defined(MQTT_ENABLED)
#include "globals.h"
#endif
namespace Yokis {
Device* busEntry(const Device& group, Device* const* devices, size_t slots) {
    if (group.getMode() != SHUTTER_BUS || !group.isConfigured() || !devices) return nullptr;
    for (size_t i=0; i<slots; ++i) {
        Device* d=devices[i];
        if (d && d->getMode()==SHUTTER && d->isConfigured() &&
            d->getChannel()==group.getChannel() &&
            memcmp(d->getHardwareAddress(),group.getHardwareAddress(),HARDWARE_ADDRESS_LENGTH)==0) return d;
    }
    return nullptr;
}
void applyBusCommand(Device& group, ShutterFeedback::Action action, bool transmitted,
                     Device* const* devices, size_t slots, uint32_t now) {
    group.setStatus(group.shutterFeedback().fromBus(action,transmitted,now));
    for (size_t i=0; devices && i<slots; ++i) {
        Device* d=devices[i];
        if (d && d->isConfigured() && d->getMode()==SHUTTER)
            d->setStatus(d->shutterFeedback().fromBus(action,transmitted,now));
    }
}
bool refreshBusState(Device& group, Device* const* devices, size_t slots) {
    if (group.getMode()!=SHUTTER_BUS) return false;
    bool any=false, allOnline=true, mixed=false;
    DeviceStatus state=UNDEFINED;
    for (size_t i=0; devices && i<slots; ++i) {
        const Device* d=devices[i];
        if (!d || !d->isConfigured() || d->getMode()!=SHUTTER) continue;
        if (any && d->getStatus()!=state) mixed=true;
        if (!any) state=d->getStatus();
        any=true; allOnline=allOnline && d->getAvailability()==ONLINE;
    }
    if (!any || mixed || !busEntry(group,devices,slots)) state=UNDEFINED;
    const DeviceAvailability availability=any && allOnline && busEntry(group,devices,slots) ? ONLINE : OFFLINE;
    const bool changed=state!=group.getStatus() || availability!=group.getAvailability();
    if (state!=group.getStatus()) {
        group.shutterFeedback().fromMembers(state);
        group.setStatus(state);
    }
    group.setAvailability(availability);
    return changed;
}
void publishBusChanges(Device* commanded) {
#if defined(ESP8266) && defined(MQTT_ENABLED)
    if (commanded && commanded->getMode()==SHUTTER_BUS && g_mqtt)
        g_mqtt->scheduleStateReplay();
    for (auto* group : g_devices) {
        if (!group || group->getMode()!=SHUTTER_BUS) continue;
        const bool changed=refreshBusState(*group,g_devices,MQTT_MAX_NUM_OF_YOKIS_DEVICES);
        if (changed && group!=commanded && g_mqtt) {
            g_mqtt->notifyPower(group);
            g_mqtt->notifyAvailability(group,group->isOffline() ? "Offline" : "Online");
        }
    }
#else
    (void)commanded;
#endif
}
}

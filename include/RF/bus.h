#pragma once
#include "RF/device.h"

namespace Yokis {
// Groups store a physical route, not a borrowed pointer or a global address.
Device* busEntry(const Device&, Device* const*, size_t);
void applyBusCommand(Device&, ShutterFeedback::Action, bool, Device* const*, size_t, uint32_t);
bool refreshBusState(Device&, Device* const*, size_t);
void publishBusChanges(Device* commanded = nullptr);
}

#ifndef YOKIS_DISCOVERY_CLEANUP_H
#define YOKIS_DISCOVERY_CLEANUP_H

#if defined(ESP8266) && defined(MQTT_ENABLED)
#include <stddef.h>
#include <stdint.h>
#include <PubSubClient.h>
#include "RF/device.h"

namespace Yokis {
enum class CleanupResult : uint8_t { Idle, Progress, Blocked };

// Persist obsolete identities before replacing configuration or freeing devices.
bool queueDiscoveryCleanup(const char* name, bool cover);
bool hasPendingDiscoveryCleanup();

// Accept at most one empty retained publish per call. PubSubClient's QoS 0
// acceptance is not an acknowledgement of durable storage by the broker.
CleanupResult serviceDiscoveryCleanup(PubSubClient& client,
                                      Device* const* active, size_t slots);
}
#endif

#endif

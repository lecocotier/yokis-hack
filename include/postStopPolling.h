#ifndef YOKIS_POST_STOP_POLLING_H
#define YOKIS_POST_STOP_POLLING_H
#if defined(ESP8266) && defined(MQTT_ENABLED)
// Main-loop services only. Never called from Ticker or a radio IRQ.
void expirePostStopChecks();
bool servicePostStopPolling();
bool postStopChecksPending();
#endif
#endif

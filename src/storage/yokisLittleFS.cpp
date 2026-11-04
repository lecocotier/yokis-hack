#ifdef ESP8266
#include "storage/yokisLittleFS.h"
bool YokisLittleFS::initialized = false;
bool YokisLittleFS::init() {
    if (!initialized) {
        // Mount failure can mean an unreadable or incompatible configuration.
        // Never autoformat it: preserve the evidence and existing RF pairings.
        LittleFSConfig config;
        config.setAutoFormat(false);
        LittleFS.setConfig(config);
        initialized = LittleFS.begin();
    }
    return initialized;
}
#endif

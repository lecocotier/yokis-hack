#ifdef ESP8266
#include "storage/yokisLittleFS.h"
bool YokisLittleFS::initialized = false;
bool YokisLittleFS::init() {
    if (!initialized) {
        LittleFSConfig config;
        config.setAutoFormat(false);
        LittleFS.setConfig(config);
        initialized = LittleFS.begin();
    }
    return initialized;
}
#endif

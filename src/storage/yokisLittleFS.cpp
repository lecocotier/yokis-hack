#ifdef ESP8266
#include "storage/yokisLittleFS.h"
bool YokisLittleFS::initialized = false;
bool YokisLittleFS::init() {
    if (!initialized) initialized = LittleFS.begin();
    return initialized;
}
#endif

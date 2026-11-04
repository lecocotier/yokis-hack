#ifndef YOKIS_ADMIN_CONFIG_H
#define YOKIS_ADMIN_CONFIG_H

#include <stddef.h>
#include <string.h>

// Real installation secrets are intentionally outside Git and compiler flags.
// Native tests supply inert fixture values and never include the local header.
#if !defined(YOKIS_NATIVE_TEST) && defined(__has_include)
#if __has_include("local_secrets.h")
#include "local_secrets.h"
#endif
#endif

#ifndef YOKIS_HTTP_USER
#define YOKIS_HTTP_USER "admin"
#endif
#ifndef YOKIS_HTTP_PASSWORD
#define YOKIS_HTTP_PASSWORD ""
#endif
#ifndef YOKIS_TELNET_PASSWORD
#define YOKIS_TELNET_PASSWORD ""
#endif
#ifndef YOKIS_OTA_PASSWORD
#define YOKIS_OTA_PASSWORD ""
#endif
#ifndef YOKIS_AP_PASSWORD
#define YOKIS_AP_PASSWORD ""
#endif

namespace Yokis {
inline bool validAdminSecret(const char* secret) {
    if (!secret) return false;
    const size_t size = strlen(secret);
    if (size < 8 || size > 63) return false;
    if (!strcmp(secret, "changeme") || !strcmp(secret, "password")) return false;
    for (size_t i = 0; i < size; ++i)
        if (secret[i] == '<' || secret[i] == '>' ||
            static_cast<unsigned char>(secret[i]) < 32) return false;
    return true;
}

template<class Ota>
bool configureAdminOta(Ota& ota, const char* password) {
    if (!validAdminSecret(password)) return false;
    ota.setPassword(password);
    ota.begin();
    return true;
}
}
#endif

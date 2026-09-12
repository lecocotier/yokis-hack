#ifdef ESP8266
#include "net/mqttConfig.h"
#include "globals.h"
#include "reliability.h"

MqttConfig::MqttConfig() : port(MQTT_DEFAULT_PORT) {
    memset(host, 0, sizeof(host)); memset(username, 0, sizeof(username));
    memset(password, 0, sizeof(password));
}
MqttConfig::MqttConfig(const MqttConfig& c) : MqttConfig() { *this = c; }
MqttConfig::MqttConfig(const char* h, uint16_t p, const char* u, const char* pw) : MqttConfig() {
    setHost(h); setPort(p); setUsername(u); setPassword(pw);
}
MqttConfig::~MqttConfig() {}
bool MqttConfig::setHost(const char* h) {
    return Yokis::configField(h ? h : "") && Yokis::copyText(host, sizeof(host), h);
}
bool MqttConfig::setPort(uint16_t p) { if (!p) return false; port = p; return true; }
bool MqttConfig::setUsername(const char* u) {
    return Yokis::configField(u ? u : "") && Yokis::copyText(username, sizeof(username), u);
}
bool MqttConfig::setPassword(const char* p) {
    return Yokis::configField(p ? p : "") && Yokis::copyText(password, sizeof(password), p);
}
char* MqttConfig::getHost() { return host; }
uint16_t MqttConfig::getPort() { return port; }
char* MqttConfig::getUsername() { return username; }
char* MqttConfig::getPassword() { return password; }
bool MqttConfig::isEmpty() { return !host[0]; }
void MqttConfig::printDebug(Print& p) {
    p.print("Host: "); p.println(host); p.print("Port: "); p.println(port);
    p.print("Username: "); p.println(username); p.print("Password: "); p.println(password);
}
bool MqttConfig::saveToLittleFS() {
    if (!YokisLittleFS::init()) return false;
    char buf[sizeof(host) + sizeof(username) + sizeof(password) + 12];
    int n = snprintf(buf, sizeof(buf), "%s|%u|%s|%s|", host, unsigned(port), username, password);
    if (n < 0 || size_t(n) >= sizeof(buf)) return false;
    const char* temp = "/mqtt.conf.tmp";
    File f = LittleFS.open(temp, "w"); if (!f) return false;
    bool ok = f.println(buf) == size_t(n) + 2;
    f.flush(); ok = ok && !f.getWriteError(); f.close();
    if (ok) ok = LittleFS.rename(temp, MQTT_CONFIG_FILE_NAME);
    if (!ok) LittleFS.remove(temp);
    return ok;
}
MqttConfig MqttConfig::loadFromLittleFS() {
    MqttConfig config;
    if (!YokisLittleFS::init()) return config;
    File f = LittleFS.open(MQTT_CONFIG_FILE_NAME, "r");
    if (!f) return config;
    char buf[MQTT_HOST_MAX_LENGTH + MQTT_USERNAME_MAX_LENGTH + MQTT_PASSWORD_MAX_LENGTH + 12];
    size_t n = 0; bool ok = true;
    while (f.available()) {
        int c = f.read();
        if (n + 1 < sizeof(buf)) buf[n++] = char(c); else ok = false;
    }
    f.close(); buf[n] = 0;
    if (!ok) return config;
    char* fields[4]; char* cur = buf;
    for (unsigned i = 0; i < 4; ++i) {
        fields[i] = cur; char* sep = strchr(cur, '|');
        if (!sep) return MqttConfig();
        *sep = 0; cur = sep + 1;
    }
    while (*cur == '\r' || *cur == '\n') ++cur;
    uint32_t port;
    if (*cur || !Yokis::unsignedNumber(fields[1], 65535, port) || !port ||
        !config.setHost(fields[0]) || !config.setPort(uint16_t(port)) ||
        !config.setUsername(fields[2]) || !config.setPassword(fields[3])) return MqttConfig();
    return config;
}
bool MqttConfig::deleteConfigFromLittleFS() {
    return YokisLittleFS::init() && (!LittleFS.exists(MQTT_CONFIG_FILE_NAME) || LittleFS.remove(MQTT_CONFIG_FILE_NAME));
}
#endif

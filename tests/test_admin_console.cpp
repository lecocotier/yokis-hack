#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "serial/adminConsole.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

FakeLog Serial;
FakeESP ESP;
FakeWiFi WiFi;
uint64_t testClockUs = 0;
std::function<void()> testYield;

namespace {
unsigned checks = 0;
unsigned connects = 0;
unsigned disconnects = 0;
const char* const password = "7fa5e2d9c0b8a641";

void require(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        std::cerr << "admin console check failed at line " << line
                  << ": " << expression << '\n';
        std::exit(1);
    }
}
#define CHECK(expression) require((expression), #expression, __LINE__)

void onConnect() { ++connects; }
void onDisconnect() { ++disconnects; }

void resetEnvironment() {
    Serial.out.clear();
    Serial.in.clear();
    ESP.restartCount = 0;
    testClockUs = 0;
    testYield = std::function<void()>();
    connects = 0;
    disconnects = 0;
}

void advance(uint64_t milliseconds) { testClockUs += milliseconds * 1000; }

void pump(AdminConsole& console, unsigned count = 8) {
    for (unsigned i = 0; i < count; ++i) console.handle();
}

WiFiClient connectClient(AdminConsole& console, uint16_t port) {
    WiFiClient client = WiFiClient::testClient();
    WiFiServer::testState(port)->pending.push_back(client);
    console.handle();
    return client;
}

void logText(AdminConsole& console, const char* text) {
    for (; *text; ++text) console.write(static_cast<uint8_t>(*text));
}

std::string drain(AdminConsole& console) {
    std::string result;
    unsigned budget = 10000;
    while (console.available() && budget--) {
        const int byte = console.read();
        CHECK(byte >= 0);
        result.push_back(static_cast<char>(byte));
    }
    CHECK(budget != 0);
    return result;
}

void authenticate(AdminConsole& console, WiFiClient& client) {
    client.feed(std::string(password) + "\r\n");
    pump(console);
    CHECK(console.authenticated());
    CHECK(console.available() == 0);
    CHECK(!console.inputPending());
}

void testDisabledAndUsb() {
    resetEnvironment();
    AdminConsole absent("", 24001);
    absent.begin(115200);
    CHECK(!WiFiServer::testState(24001)->listening);
    CHECK(WiFiServer::testState(24001)->beginCalls == 0);
    AdminConsole shortPassword("1234567", 24002);
    shortPassword.begin(115200);
    CHECK(WiFiServer::testState(24002)->beginCalls == 0);
    AdminConsole placeholder("changeme", 24003);
    placeholder.begin(115200);
    CHECK(WiFiServer::testState(24003)->beginCalls == 0);
    Serial.feed("USB\r\nNEXT\n");
    pump(absent);
    CHECK(drain(absent) == "USB\n");
    pump(absent);
    CHECK(drain(absent) == "NEXT\n");
    CHECK(absent.read() == -1);
    logText(absent, "usb-output");
    CHECK(Serial.out.find("usb-output") != std::string::npos);
}

void testAuthenticationAndIsolation() {
    resetEnvironment();
    AdminConsole console(password, 24004);
    console.setCallbackOnConnect(onConnect);
    console.setCallbackOnDisconnect(onDisconnect);
    console.begin(115200);
    CHECK(WiFiServer::testState(24004)->listening);
    CHECK(WiFiServer::testState(24004)->beginCalls == 1);
    console.begin(115200);
    CHECK(WiFiServer::testState(24004)->beginCalls == 1);
    WiFiClient client = connectClient(console, 24004);
    CHECK(client.connected());
    CHECK(!console.authenticated());
    CHECK(connects == 0);
    logText(console, "unavailable-before-login");
    CHECK(client.output().find("unavailable-before-login") == std::string::npos);
    client.feed("7fa5e2d9");
    pump(console);
    CHECK(!console.authenticated());
    CHECK(!console.inputPending());
    CHECK(console.available() == 0);
    CHECK(Serial.out.find("7fa5e2d9") == std::string::npos);
    client.feed("c0b8a641\r\n");
    pump(console);
    CHECK(console.authenticated());
    CHECK(connects == 1);
    CHECK(console.available() == 0);
    CHECK(client.output().find(password) == std::string::npos);
    logText(console, "visible-after-login");
    pump(console);
    CHECK(client.output().find("visible-after-login") != std::string::npos);
    CHECK(client.output().find("unavailable-before-login") == std::string::npos);

    Serial.feed("USB");
    client.feed("TCP");
    pump(console);
    CHECK(console.available() == 0);
    CHECK(console.inputPending());
    Serial.feed(" one\nUSB two\n");
    client.feed(" one\nTCP two\n");
    pump(console);
    const std::string first = drain(console);
    CHECK(first.find("USB one\n") != std::string::npos);
    CHECK(first.find("TCP one\n") != std::string::npos);
    CHECK(first.find("USBTCP") == std::string::npos);
    pump(console);
    const std::string second = drain(console);
    CHECK(second.find("USB two\n") != std::string::npos);
    CHECK(second.find("TCP two\n") != std::string::npos);
    CHECK(!console.inputPending());

    WiFiClient extra = connectClient(console, 24004);
    CHECK(!extra.connected());
    CHECK(client.connected());
    CHECK(console.authenticated());
    CHECK(connects == 1);
    CHECK(disconnects == 0);
    client.stop();
    console.handle();
    CHECK(!console.authenticated());
    CHECK(disconnects == 1);
    console.handle();
    CHECK(disconnects == 1);
}

void testWrongPasswordAndCooldown() {
    resetEnvironment();
    AdminConsole console(password, 24005);
    console.setCallbackOnConnect(onConnect);
    console.setCallbackOnDisconnect(onDisconnect);
    console.begin(115200);
    WiFiClient client = connectClient(console, 24005);
    client.feed("wrong\r\nA06 600000\n");
    pump(console);
    CHECK(!client.connected());
    CHECK(!console.authenticated());
    CHECK(console.available() == 0);
    CHECK(!console.inputPending());
    CHECK(connects == 0 && disconnects == 0);
    CHECK(Serial.out.find("wrong") == std::string::npos);
    WiFiClient early = connectClient(console, 24005);
    CHECK(!early.connected());
    advance(2000);
    WiFiClient retry = connectClient(console, 24005);
    CHECK(retry.connected());
    authenticate(console, retry);
    CHECK(connects == 1);
}

void testTimeoutAndRollover() {
    resetEnvironment();
    testClockUs = static_cast<uint64_t>(0xfffffff0UL) * 1000;
    AdminConsole console(password, 24006);
    console.begin(115200);
    WiFiClient client = connectClient(console, 24006);
    client.feed("7fa5");
    pump(console);
    advance(29999);
    console.handle();
    CHECK(client.connected());
    advance(1);
    console.handle();
    CHECK(!client.connected());
    CHECK(!console.inputPending());
    advance(1999);
    WiFiClient early = connectClient(console, 24006);
    CHECK(!early.connected());
    advance(1);
    WiFiClient retry = connectClient(console, 24006);
    CHECK(retry.connected());
    authenticate(console, retry);

    Serial.feed("OLD USB");
    retry.feed("OLD TCP");
    pump(console);
    CHECK(console.inputPending());
    advance(5000);
    console.handle();
    CHECK(!console.inputPending());
    Serial.feed(" tail\nUSB fresh\n");
    retry.feed(" tail\nTCP fresh\n");
    pump(console);
    const std::string lines = drain(console);
    CHECK(lines.find("USB fresh\n") != std::string::npos);
    CHECK(lines.find("TCP fresh\n") != std::string::npos);
    CHECK(lines.find("tail") == std::string::npos);
    CHECK(lines.find("OLD") == std::string::npos);
}

void testTelnetNegotiationAndCrLf() {
    resetEnvironment();
    AdminConsole console(password, 24007);
    console.begin(115200);
    WiFiClient client = connectClient(console, 24007);
    const std::string willEcho("\xff\xfb\x01", 3);
    CHECK(client.output().find(willEcho) != std::string::npos);
    client.feed(std::string("\xff\xf4", 2)); // IAC IP must not reboot.
    client.feed(std::string("\xff\xfb\x01", 3));
    client.feed(std::string("\xff\xfa\x01", 3) + "ignored" +
                std::string("\xff\xf0", 2));
    authenticate(console, client);
    CHECK(ESP.restartCount == 0);
    client.feed(std::string("\xff\xf4", 2) + "FIRST\r\nSECOND\n");
    pump(console);
    CHECK(drain(console) == "FIRST\n");
    pump(console);
    CHECK(drain(console) == "SECOND\n");
    CHECK(ESP.restartCount == 0);
}

void testOverflowAndDisconnect() {
    resetEnvironment();
    AdminConsole console(password, 24008);
    console.setCallbackOnConnect(onConnect);
    console.setCallbackOnDisconnect(onDisconnect);
    console.begin(115200);
    WiFiClient client = connectClient(console, 24008);
    authenticate(console, client);
    Serial.feed(std::string(300, 'x') + "A06 600000\nSAFE\n");
    pump(console, 16);
    const std::string overlong = drain(console);
    CHECK(overlong.size() == 257);
    CHECK(overlong[overlong.size() - 1] == '\n');
    CHECK(overlong.find("A06") == std::string::npos);
    pump(console);
    CHECK(drain(console) == "SAFE\n");

    client.feed("ACCEPTED\n");
    console.handle();
    client.stop();
    console.handle();
    CHECK(drain(console) == "ACCEPTED\n");
    CHECK(disconnects == 1);
    WiFiClient next = connectClient(console, 24008);
    authenticate(console, next);
    next.feed("INCOMPLETE");
    pump(console);
    CHECK(console.inputPending());
    next.stop();
    console.handle();
    CHECK(!console.inputPending());
    CHECK(console.available() == 0);
    CHECK(disconnects == 2);
    WiFiClient last = connectClient(console, 24008);
    authenticate(console, last);
    last.feed("NEW\n");
    pump(console);
    CHECK(drain(console) == "NEW\n");
    CHECK(connects == 3);
}

void testBackpressureDoesNotReplayLogs() {
    resetEnvironment();
    AdminConsole console(password, 24009);
    console.begin(115200);
    WiFiClient client = connectClient(console, 24009);
    authenticate(console, client);
    client.setAvailableForWrite(0);
    logText(console, "do-not-replay-this-log");
    pump(console);
    CHECK(Serial.out.find("do-not-replay-this-log") != std::string::npos);
    CHECK(client.output().find("do-not-replay-this-log") == std::string::npos);
    for (unsigned i = 0; i < 32; ++i) {
        client.setAvailableForWrite(5);
        console.handle();
    }
    CHECK(client.output().find("retry via USB") != std::string::npos);
    CHECK(client.output().find("do-not-replay-this-log") == std::string::npos);
    client.setAvailableForWrite(128);
    logText(console, "fresh-log");
    CHECK(client.output().find("fresh-log") != std::string::npos);
}

void testCommandLengthBoundary() {
    resetEnvironment();
    AdminConsole console(password, 24010);
    console.begin(115200);
    Serial.feed(std::string(255, 'a') + "\n");
    pump(console, 16);
    CHECK(drain(console) == std::string(255, 'a') + "\n");
    Serial.feed(std::string(256, 'b') + "tail\nSAFE\n");
    pump(console, 16);
    const std::string rejected = drain(console);
    CHECK(rejected.size() == 257);
    CHECK(rejected.find("tail") == std::string::npos);
    pump(console);
    CHECK(drain(console) == "SAFE\n");
}
}

int main() {
    testDisabledAndUsb();
    testAuthenticationAndIsolation();
    testWrongPasswordAndCooldown();
    testTimeoutAndRollover();
    testTelnetNegotiationAndCrLf();
    testOverflowAndDisconnect();
    testBackpressureDoesNotReplayLogs();
    testCommandLengthBoundary();
    std::cout << checks << " admin console checks passed\n";
    return 0;
}

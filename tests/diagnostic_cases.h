#pragma once

#include "net/diagnostics.h"
#include "RF/device.h"
#include <cstring>
#include <cstdio>
#include <fstream>

// Keep diagnostics test inputs independent of live network and radio objects.
static void testDiagnosticSerialization() {
    Yokis::DiagnosticsSnapshot snapshot = {};
    snapshot.buildId = "test-build";
    snapshot.gitRevision = "test-git";
    snapshot.sourceSha256 = "test-source";
    snapshot.ssid = "wifi\"\\\n\t\x01";
    snapshot.ip = "192.0.2.1";
    snapshot.mqttHost = "broker\"\\\r\b\f\x02";
    snapshot.uptimeMs = 12345;
    snapshot.freeHeap = 30000;
    snapshot.rssi = -67;
    snapshot.mqttPort = 1883;
    snapshot.debugLevel = 0;
    snapshot.wifiConnected = true;
    snapshot.mqttConnected = false;
    snapshot.mqttConfigured = true;

    char output[4096];
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, nullptr, 0, 0));
    CHECK(std::strstr(output, "\"build\":\"test-build\"") != nullptr);
    CHECK(std::strstr(output, "\"git\":\"test-git\"") != nullptr);
    CHECK(std::strstr(output, "\"source_sha256\":\"test-source\"") != nullptr);
    CHECK(std::strstr(output, "\"debug_level\":0") != nullptr);
    CHECK(std::strstr(output, "\"ssid\":\"wifi\\\"\\\\\\n\\t\\u0001\"") != nullptr);
    CHECK(std::strstr(output, "\"host\":\"broker\\\"\\\\\\r\\b\\f\\u0002\"") != nullptr);
    CHECK(std::strstr(output, "\"has_next\":false") != nullptr);
    CHECK(std::strstr(output, "\"devices\":[]") != nullptr);
    CHECK(std::strstr(output, "password") == nullptr);
    CHECK(std::strstr(output, "token") == nullptr);
    CHECK(std::strstr(output, "secret") == nullptr);
    {
        std::ofstream fixture("tests/build/diagnostics-hostile-empty.json", std::ios::binary);
        fixture << output;
        fixture.close();
        CHECK(!fixture.fail());
    }

    CHECK(!Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, nullptr, 0, 1));
    CHECK(output[0] == '\0');
    CHECK(!Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, nullptr, 1, 0));
    CHECK(output[0] == '\0');
    CHECK(!Yokis::writeDiagnosticJson(nullptr, 0, snapshot, nullptr, 0, 0));
    CHECK(!Yokis::writeDiagnosticJson(nullptr, 10, snapshot, nullptr, 0, 0));

    struct GuardedOutput {
        char before;
        char data[16];
        char after;
    } guarded = {'L', {}, 'R'};
    std::memset(guarded.data, 'X', sizeof(guarded.data));
    CHECK(!Yokis::writeDiagnosticJson(guarded.data, sizeof(guarded.data), snapshot, nullptr, 0, 0));
    CHECK(guarded.data[0] == '\0');
    CHECK(guarded.before == 'L');
    CHECK(guarded.after == 'R');
    guarded.data[0] = 'X';
    CHECK(!Yokis::writeDiagnosticJson(guarded.data, 0, snapshot, nullptr, 0, 0));
    CHECK(guarded.data[0] == 'X');
    CHECK(!Yokis::writeDiagnosticJson(guarded.data, 1, snapshot, nullptr, 0, 0));
    CHECK(guarded.data[0] == '\0');

    Device *nullDevices[3] = {};
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, nullDevices, 3, 0));
    CHECK(std::strstr(output, "\"total\":0") != nullptr);
    CHECK(!Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, nullDevices, 3, 1));
    CHECK(output[0] == '\0');

    const uint8_t diagnosticAddress[] = {1, 2, 1, 2, 2};
    Device first("premier", diagnosticAddress, 1);
    Device second("second", diagnosticAddress, 1);
    first.setMode(ON_OFF);
    first.setStatus(OFF);
    second.setMode(SHUTTER);
    second.setStatus(UNDEFINED);
    Device *sparse[] = {nullptr, &first, nullptr, &second, nullptr};
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, sparse, 5, 0));
    CHECK(std::strstr(output, "\"total\":2") != nullptr);
    CHECK(std::strstr(output, "\"name\":\"premier\"") != nullptr);
    CHECK(std::strstr(output, "\"name\":\"second\"") != nullptr);
    CHECK(std::strstr(output, "\"estimated\":false") != nullptr);
    CHECK(std::strstr(output, "\"status\":\"UNDEFINED\"") != nullptr);

    auto &feedback = second.shutterFeedback();
    feedback.begin(Yokis::ShutterFeedback::Down, snapshot.uptimeMs);
    second.setStatus(feedback.finish(true, snapshot.uptimeMs));
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, sparse, 5, 0));
    CHECK(std::strstr(output, "\"estimated\":true") != nullptr);
    CHECK(std::strstr(output, "\"state_source\":\"command_estimate\"") != nullptr);
    CHECK(std::strstr(output, "\"status\":\"closing\"") != nullptr);

    Device *many[65] = {};
    char maxSsid[33];
    char maxHost[64];
    std::memset(maxSsid, 's', sizeof(maxSsid) - 1);
    maxSsid[sizeof(maxSsid) - 1] = '\0';
    std::memset(maxHost, 'h', sizeof(maxHost) - 1);
    maxHost[sizeof(maxHost) - 1] = '\0';
    snapshot.ssid = maxSsid;
    snapshot.mqttHost = maxHost;
    for (size_t i = 0; i < 65; ++i) {
        char name[49];
        std::snprintf(name, sizeof(name), "device-%02u-abcdefghijklmnopqrstuvwxyz-0123456789x", static_cast<unsigned>(i));
        many[i] = new Device(name, diagnosticAddress, 1);
        many[i]->setMode(ON_OFF);
        many[i]->setStatus(OFF);
    }
    for (size_t page = 0; page < 8; ++page) {
        CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, many, 64, page));
        {
            char fixturePath[80];
            std::snprintf(fixturePath, sizeof(fixturePath), "tests/build/diagnostics-page%u.json", static_cast<unsigned>(page));
            std::ofstream fixture(fixturePath, std::ios::binary);
            fixture << output;
            fixture.close();
            CHECK(!fixture.fail());
        }
        CHECK(std::strstr(output, "\"total\":64") != nullptr);
        CHECK(std::strstr(output, "\"page_size\":8") != nullptr);
        CHECK(std::strstr(output, page < 7 ? "\"has_next\":true" : "\"has_next\":false") != nullptr);
        for (size_t i = 0; i < 64; ++i) {
            char marker[32];
            std::snprintf(marker, sizeof(marker), "\"name\":\"device-%02u-", static_cast<unsigned>(i));
            CHECK((std::strstr(output, marker) != nullptr) == (i / 8 == page));
        }
    }
    CHECK(!Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, many, 64, 8));
    CHECK(output[0] == '\0');
    CHECK(!Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, many, 64, static_cast<size_t>(-1)));
    CHECK(output[0] == '\0');
    CHECK(!Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, many, 65, 0));
    CHECK(output[0] == '\0');
    Device *sparseMany[128] = {};
    for (size_t i = 0; i < 64; ++i) sparseMany[2 * i + 1] = many[i];
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, sparseMany, 128, 7));
    CHECK(std::strstr(output, "\"total\":64") != nullptr);
    CHECK(std::strstr(output, "\"name\":\"device-56-") != nullptr);
    CHECK(std::strstr(output, "\"name\":\"device-63-") != nullptr);
    CHECK(std::strstr(output, "\"name\":\"device-55-") == nullptr);
    for (size_t i = 0; i < 65; ++i) delete many[i];

    extern uint64_t testClockUs;
    const uint64_t savedClock = testClockUs;
    testClockUs = static_cast<uint64_t>(0xfffffff0UL) * 1000ULL;
    first.setStatus(ON);
    testClockUs += 32ULL * 1000ULL;
    snapshot.uptimeMs = 16;
    Device *one[] = {&first};
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, one, 1, 0));
    CHECK(std::strstr(output, "\"status_age_ms\":32") != nullptr);
    testClockUs = savedClock;

    snapshot.ssid = "caf\xc3\xa9";
    snapshot.mqttHost = nullptr;
    CHECK(Yokis::writeDiagnosticJson(output, sizeof(output), snapshot, nullptr, 0, 0));
    CHECK(std::strstr(output, "\"ssid\":\"caf\xc3\xa9\"") != nullptr);
    CHECK(std::strstr(output, "\"host\":\"\"") != nullptr);
    const size_t exactSize = std::strlen(output) + 1;
    CHECK(!Yokis::writeDiagnosticJson(output, exactSize - 1, snapshot, nullptr, 0, 0));
    CHECK(output[0] == '\0');
    CHECK(Yokis::writeDiagnosticJson(output, exactSize, snapshot, nullptr, 0, 0));
}

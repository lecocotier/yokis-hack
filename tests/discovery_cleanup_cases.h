#ifndef YOKIS_DISCOVERY_CLEANUP_CASES_H
#define YOKIS_DISCOVERY_CLEANUP_CASES_H

#include "net/discoveryCleanup.h"
#include <LittleFS.h>
#include <memory>
#include <string>

namespace DiscoveryCleanupTests {
const char* const journal = "/discovery.cleanup";
const char* const temporary = "/discovery.cleanup.tmp";

struct Fixture {
    decltype(testFs.files) savedFiles;
    bool savedMount;
    bool savedWrites;
    bool savedRename;
    bool savedOpen;
    Fixture()
        : savedFiles(testFs.files), savedMount(testFs.failMount),
          savedWrites(testFs.failWrites), savedRename(testFs.failRename),
          savedOpen(testFs.failOpen) {
        reset();
    }
    void reset() {
        testFs.files.erase(journal);
        testFs.files.erase(temporary);
        testFs.failMount = false;
        testFs.failWrites = false;
        testFs.failRename = false;
        testFs.failOpen = false;
    }
    ~Fixture() {
        testFs.files = savedFiles;
        testFs.failMount = savedMount;
        testFs.failWrites = savedWrites;
        testFs.failRename = savedRename;
        testFs.failOpen = savedOpen;
    }
};

std::string contents() {
    const auto found = testFs.files.find(journal);
    return found == testFs.files.end() ? std::string() : *found->second;
}

void setContents(const std::string& text) {
    testFs.files[journal] = std::make_shared<std::string>(text);
}

void checkEmptyRetained(const PubSubClient& client, size_t index, const char* topic) {
    CHECK(client.publications.size() > index);
    CHECK(client.retained.size() > index);
    if (client.publications.size() <= index || client.retained.size() <= index) return;
    CHECK(client.publications[index].first == topic);
    CHECK(client.publications[index].second.empty());
    CHECK(client.retained[index]);
}
}

void testDiscoveryCleanup() {
    using Yokis::CleanupResult;
    using namespace DiscoveryCleanupTests;
    Fixture fixture;
    WiFiClient transport;
    PubSubClient client(transport);
    client.connected_ = true;

    CHECK(!Yokis::hasPendingDiscoveryCleanup());
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Idle);
    CHECK(!Yokis::queueDiscoveryCleanup(nullptr, false));
    CHECK(!Yokis::queueDiscoveryCleanup("", false));
    CHECK(!Yokis::queueDiscoveryCleanup(std::string(49, 'a').c_str(), false));
    CHECK(!Yokis::queueDiscoveryCleanup("bad|name", false));
    CHECK(!Yokis::hasPendingDiscoveryCleanup());
    CHECK(Yokis::queueDiscoveryCleanup("original", false));
    const std::string original = contents();
    CHECK(original.find("YKDISC1") == 0);
    CHECK(Yokis::queueDiscoveryCleanup("original", false));
    CHECK(contents() == original);
    CHECK(Yokis::queueDiscoveryCleanup("original", true));
    CHECK(contents().find("original|l|0") != std::string::npos);
    CHECK(contents().find("original|c|0") != std::string::npos);

    fixture.reset();
    CHECK(Yokis::queueDiscoveryCleanup("stable", false));
    const std::string stable = contents();
    testFs.failWrites = true;
    CHECK(!Yokis::queueDiscoveryCleanup("new_identity", true));
    CHECK(contents() == stable);
    testFs.failWrites = false;
    testFs.failRename = true;
    CHECK(!Yokis::queueDiscoveryCleanup("new_identity", true));
    CHECK(contents() == stable);
    testFs.failRename = false;
    testFs.failOpen = true;
    CHECK(!Yokis::queueDiscoveryCleanup("new_identity", true));
    CHECK(contents() == stable);
    testFs.failOpen = false;

    fixture.reset();
    for (unsigned i = 0; i < 128; ++i) {
        const std::string name = "queued_" + std::to_string(i);
        CHECK(Yokis::queueDiscoveryCleanup(name.c_str(), false));
    }
    const std::string full = contents();
    CHECK(!Yokis::queueDiscoveryCleanup("one_too_many", false));
    CHECK(contents() == full);
    CHECK(Yokis::queueDiscoveryCleanup("queued_0", false));
    CHECK(contents() == full);

    fixture.reset();
    CHECK(Yokis::queueDiscoveryCleanup("removed", false));
    const std::string queued = contents();
    client.connected_ = false;
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Blocked);
    CHECK(contents() == queued);
    CHECK(client.publications.empty());
    client.connected_ = true;
    client.failPublish = true;
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Blocked);
    CHECK(contents() == queued);
    client.failPublish = false;
    client.publications.clear();
    client.retained.clear();
    testFs.failRename = true;
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Blocked);
    CHECK(contents() == queued);
    checkEmptyRetained(client, 0, "homeassistant/light/removed/config");
    testFs.failRename = false;
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Progress);
    checkEmptyRetained(client, 1, "homeassistant/light/removed/config");
    CHECK(contents().find("removed|l|1") != std::string::npos);
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Progress);
    checkEmptyRetained(client, 2, "removed/tele/STATE");

    // A fresh client simulates losing all in-memory state between service calls.
    WiFiClient restartedTransport;
    PubSubClient restarted(restartedTransport);
    restarted.connected_ = true;
    CHECK(Yokis::serviceDiscoveryCleanup(restarted, nullptr, 0) == CleanupResult::Progress);
    checkEmptyRetained(restarted, 0, "removed/tele/DETAIL");
    CHECK(Yokis::serviceDiscoveryCleanup(restarted, nullptr, 0) == CleanupResult::Progress);
    checkEmptyRetained(restarted, 1, "removed/tele/BRIGHTNESS");
    CHECK(Yokis::serviceDiscoveryCleanup(restarted, nullptr, 0) == CleanupResult::Progress);
    checkEmptyRetained(restarted, 2, "removed/tele/LWT");
    CHECK(!Yokis::hasPendingDiscoveryCleanup());
    CHECK(Yokis::serviceDiscoveryCleanup(restarted, nullptr, 0) == CleanupResult::Idle);

    fixture.reset();
    client.publications.clear();
    client.retained.clear();
    CHECK(Yokis::queueDiscoveryCleanup("recreated", false));
    const uint8_t address[] = {1, 2, 1, 2, 2};
    Device recreated("recreated", address, 1);
    recreated.setMode(DIMMER);
    Device* same[] = {nullptr, &recreated};
    CHECK(Yokis::serviceDiscoveryCleanup(client, same, 2) == CleanupResult::Progress);
    CHECK(client.publications.empty());
    CHECK(!Yokis::hasPendingDiscoveryCleanup());

    CHECK(Yokis::queueDiscoveryCleanup("retyped", false));
    Device retyped("retyped", address, 1);
    retyped.setMode(SHUTTER);
    Device* replacement[] = {&retyped};
    CHECK(Yokis::serviceDiscoveryCleanup(client, replacement, 1) == CleanupResult::Progress);
    CHECK(client.publications.size() == 1);
    checkEmptyRetained(client, 0, "homeassistant/light/retyped/config");
    CHECK(!Yokis::hasPendingDiscoveryCleanup());
    CHECK(Yokis::queueDiscoveryCleanup("recreated", true));
    CHECK(Yokis::serviceDiscoveryCleanup(client, same, 2) == CleanupResult::Progress);
    CHECK(client.publications.size() == 2);
    checkEmptyRetained(client, 1, "homeassistant/cover/recreated/config");

    // Reconcile again before every stage, including a recreation after progress.
    fixture.reset();
    client.publications.clear();
    client.retained.clear();
    CHECK(Yokis::queueDiscoveryCleanup("recreated", false));
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Progress);
    CHECK(Yokis::serviceDiscoveryCleanup(client, same, 2) == CleanupResult::Progress);
    CHECK(client.publications.size() == 1);
    CHECK(!Yokis::hasPendingDiscoveryCleanup());
    CHECK(Yokis::queueDiscoveryCleanup("retyped", false));
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Progress);
    CHECK(Yokis::serviceDiscoveryCleanup(client, replacement, 1) == CleanupResult::Progress);
    CHECK(client.publications.size() == 2);
    CHECK(!Yokis::hasPendingDiscoveryCleanup());

    fixture.reset();
    client.publications.clear();
    client.retained.clear();
    {
        char transient[] = "transient";
        CHECK(Yokis::queueDiscoveryCleanup(transient, true));
        transient[0] = 'X';
    }
    CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Progress);
    checkEmptyRetained(client, 0, "homeassistant/cover/transient/config");

    const std::string corrupt[] = {
        "wrong-header\nname|l|0\n", "YKDISC1\nname|x|0\n",
        "YKDISC1\nname|l|5\n", "YKDISC1\nname|l|0|extra\n",
        "YKDISC1\n" + std::string(49, 'n') + "|l|0\n",
        "YKDISC1\nname|l|0\nname|l|0\n"
    };
    for (const std::string& malformed : corrupt) {
        fixture.reset();
        client.publications.clear();
        client.retained.clear();
        setContents(malformed);
        CHECK(Yokis::hasPendingDiscoveryCleanup());
        CHECK(!Yokis::queueDiscoveryCleanup("do_not_overwrite", false));
        CHECK(Yokis::serviceDiscoveryCleanup(client, nullptr, 0) == CleanupResult::Blocked);
        CHECK(contents() == malformed);
        CHECK(client.publications.empty());
    }
}

#endif

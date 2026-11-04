#pragma once

void testCleanupIntegration() {
    const auto savedFiles = testFs.files;
    Device* saved[64]; Ticker* timers[64];
    std::copy(g_devices, g_devices+64, saved);
    std::copy(g_deviceStatusPollers, g_deviceStatusPollers+64, timers);
    std::fill(g_devices, g_devices+64, nullptr);
    std::fill(g_deviceStatusPollers, g_deviceStatusPollers+64, nullptr);
    testFs.files.clear();
    Device old("removed-offline", address, 47); old.setMode(SHUTTER);
    CHECK(old.saveToLittleFS());
    CHECK(reloadConfig(nullptr));
    g_mqtt->connected_ = false;
    CHECK(Device::deleteFromConfig(old.getName()));
    CHECK(Yokis::hasPendingDiscoveryCleanup());
    CHECK(reloadConfig(nullptr));
    CHECK(g_devices[0] == nullptr);
    g_mqtt->connected_ = true; g_mqtt->input.stopped = false;
    g_mqtt->publications.clear();
    for (unsigned i=0; i<8; ++i) {
        testClockUs += 200000;
        g_mqtt->serviceRefresh(g_devices, 64);
    }
    CHECK(!Yokis::hasPendingDiscoveryCleanup());
    bool cleared = false;
    for (const auto& p : g_mqtt->publications)
        if (p.first == "homeassistant/cover/removed-offline/config" && p.second.empty()) cleared = true;
    CHECK(cleared);

    CHECK(old.saveToLittleFS()); CHECK(reloadConfig(nullptr));
    const std::string committed = *testFs.files["/yokis.conf"];
    testFs.failRename = true;
    CHECK(!Device::clearConfigFromLittleFS());
    CHECK(*testFs.files["/yokis.conf"] == committed);
    CHECK(g_devices[0] && std::string(g_devices[0]->getName()) == old.getName());
    testFs.failRename = false;

    // Retyping owns only the obsolete component, never the new shared states.
    old.setMode(DIMMER); CHECK(old.saveToLittleFS());
    CHECK(Yokis::hasPendingDiscoveryCleanup()); CHECK(reloadConfig(nullptr));
    g_mqtt->publications.clear();
    for (unsigned i=0; i<5; ++i) {
        testClockUs += 200000; g_mqtt->serviceRefresh(g_devices, 64);
    }
    cleared = false;
    for (const auto& p : g_mqtt->publications) {
        if (p.first == "homeassistant/cover/removed-offline/config" && p.second.empty()) cleared = true;
        CHECK(p.first.find("/tele/") == std::string::npos || !p.second.empty());
    }
    CHECK(cleared);

    g_bp->setDevice(nullptr);
    for (unsigned i=0; i<64; ++i) { delete g_devices[i]; delete g_deviceStatusPollers[i]; }
    std::copy(saved, saved+64, g_devices);
    std::copy(timers, timers+64, g_deviceStatusPollers);
    testFs.files = savedFiles;
    g_mqtt->setDiscoveryDone(false);
}

// Accepting a long button hold must not spend its whole duration in a callback.
void testCooperativeHold(Device* d, WiFiClient& wifi) {
    g_mqtt->setCallback(mqttCallback); // Production setup() registers this too.
    freshDevice();
    d->setMode(DIMMER);
    for (unsigned i = 0; i < 8; ++i) testRadio.replies.push_back({1, 0});
    const uint32_t started = millis();
    CHECK(pressForCallback("pressFor volet 800"));
    CHECK(uint32_t(millis() - started) < 100);
    CHECK(testRadio.writes == 1);
    CHECK(g_bp->holdActive());
    const uint32_t accepted = millis();
    testClockUs += 799000;
    CHECK(g_bp->serviceHold() == nullptr);
    testClockUs += 1000;
    CHECK(uint32_t(millis() - accepted) >= 800);
    CHECK(g_bp->serviceHold() == d);
    CHECK(!g_bp->holdActive());
    CHECK(testRadio.writes == 2);
    CHECK(testRadio.lastPayload[0] == 0x53);

    // One due pulse per service call; delayed work never catches up in a burst.
    freshDevice(); d->setMode(DIMMER);
    testRadio.replies.push_back({1, 0});
    CHECK(pressForCallback("pressFor volet 3000"));
    testClockUs += 1500000;
    testRadio.replies.push_back({1, 0});
    CHECK(g_bp->serviceHold() == d);
    CHECK(testRadio.lastPayload[7] == 2);
    const unsigned afterPulse = testRadio.writes;
    CHECK(g_bp->serviceHold() == nullptr);
    CHECK(testRadio.writes == afterPulse);
    testClockUs += 3000000;
    testRadio.replies.push_back({1, 0});
    CHECK(g_bp->serviceHold() == d);
    CHECK(!g_bp->holdActive());

    // A command to another shutter is sent before the cancelled hold release.
    freshDevice(); d->setMode(DIMMER);
    testRadio.replies.push_back({1, 0});
    CHECK(pressForCallback("pressFor volet 5000"));
    const uint8_t otherAddress[] = {3, 4, 3, 4, 4};
    Device other("hold-other", otherAddress, 48); other.setMode(SHUTTER);
    g_bp->setDevice(&other);
    testRadio.replies.push_back({0, 1});
    CHECK(g_bp->pauseShutter());
    CHECK(testRadio.lastPayload[0] == 0x1a);
    CHECK(g_bp->holdActive());
    testRadio.replies.push_back({1, 0});
    CHECK(g_bp->serviceHold() == d);
    CHECK(testRadio.lastPayload[0] == 0x53);
    CHECK(g_bp->getDevice() == &other);
    CHECK(other.shutterFeedback().verification().pending());
    CHECK(!g_bp->holdActive());

    // Explicit release cancels one queued hold; it must not send two releases.
    freshDevice(); d->setMode(DIMMER);
    testRadio.replies.push_back({1, 0});
    CHECK(pressForCallback("pressFor volet 5000"));
    CHECK(releaseCallback("release volet"));
    CHECK(testRadio.writes == 1);
    testRadio.replies.push_back({1, 0});
    CHECK(g_bp->serviceHold() == d);
    CHECK(testRadio.writes == 2);
    CHECK(g_bp->serviceHold() == nullptr);

    // A normal loop iteration still services OTA/MQTT while a hold is waiting.
    freshDevice(); d->setMode(DIMMER);
    g_ConfigFlags = FLAG_POLLING;
    g_mqtt->setConnectionInfo("broker", 1883, "", "", false);
    g_mqtt->connected_ = true; g_mqtt->setDiscoveryDone(true);
    testRadio.replies.push_back({1, 0});
    CHECK(pressForCallback("pressFor volet 5000"));
    d->pollMePlease();
    const unsigned ota = ArduinoOTA.handleCalls;
    loop();
    CHECK(ArduinoOTA.handleCalls > ota);
    CHECK(testRadio.writes == 1); // No background poll steals hold ownership.
    CHECK(g_bp->cancelHold());
    testRadio.replies.push_back({1, 0});
    CHECK(g_bp->serviceHold() == d);

    // Through the real loop: queued STOP, its verification, then hold release.
    freshDevice(); d->setMode(DIMMER);
    Device urgent("urgent-hold", otherAddress, 48); urgent.setMode(SHUTTER);
    g_devices[1] = &urgent;
    g_mqtt->connected_ = true; g_mqtt->setDiscoveryDone(true);
    testRadio.replies.push_back({1, 0});
    CHECK(pressForCallback("pressFor volet 5000"));
    wifi.packets.push_back({"urgent-hold/cmnd/POWER", "PAUSE"});
    testRadio.replies.push_back({0, 1}); loop();
    CHECK(testRadio.writes == 2);
    CHECK(testRadio.lastPayload[0] == 0x1a);
    CHECK(g_bp->holdActive());
    testClockUs += 200000; testRadio.replies.push_back({0, 0}); loop();
    CHECK(testRadio.writes == 3);
    CHECK(testRadio.lastPayload[0] == 0);
    CHECK(g_bp->holdActive()); // No second background transaction this turn.
    testRadio.replies.push_back({1, 0}); loop();
    CHECK(testRadio.writes == 4);
    CHECK(testRadio.lastPayload[0] == 0x53);
    CHECK(!g_bp->holdActive());
    CHECK(urgent.getStatus() == SHUTTER_STOPPED);
    g_devices[1] = nullptr;

    // Lifetime invalidation discards the borrowed target before it is freed.
    Device* temporary = new Device("temporary-hold", otherAddress, 49);
    temporary->setMode(DIMMER); g_bp->setDevice(temporary);
    testRadio.replies.push_back({1, 0});
    CHECK(g_bp->pressAndHoldFor(800));
    g_bp->setDevice(nullptr); delete temporary;
    CHECK(!g_bp->holdActive());
    CHECK(g_bp->serviceHold() == nullptr);

    // Duration comparisons survive the 32-bit millis rollover.
    freshDevice(); d->setMode(DIMMER);
    testClockUs = (uint64_t(UINT32_MAX) - 100) * 1000;
    testRadio.replies.push_back({1, 0});
    CHECK(pressForCallback("pressFor volet 800"));
    testClockUs += 900000; testRadio.replies.push_back({1, 0});
    CHECK(g_bp->serviceHold() == d);
    CHECK(!g_bp->holdActive());
    freshDevice();
}

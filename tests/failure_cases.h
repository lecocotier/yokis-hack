// Production callbacks must release special RF ownership on every exit and
// publish uncertainty after failed console commands, just as MQTT does.
void testFailureRecovery(Device* d) {
    g_ConfigFlags = FLAG_POLLING;
    g_mqtt->connected_ = true;
    g_mqtt->setDiscoveryDone(true);

    freshDevice();
    CHECK(copyCallback("copy volet"));
    CHECK(IrqManager::irqType == E2BP);
    CHECK(testRadio.config == 0x0e);
    CHECK(g_bp->getDevice() == d);

    freshDevice();
    testRadio.connected = false;
    CHECK(!copyCallback("copy volet"));
    CHECK(IrqManager::irqType == E2BP);
    CHECK(g_bp->getDevice() == d);

    freshDevice();
    testRadio.connected = false;
    CHECK(!pairingCallback(nullptr));
    CHECK(IrqManager::irqType == E2BP);

    freshDevice();
    CHECK(!pairingCallback(nullptr)); // A real timeout in the simulated clock.
    CHECK(IrqManager::irqType == E2BP);
    CHECK(testRadio.config == 0x0e);

    freshDevice();
    d->setStatus(SHUTTER_OPENED);
    g_mqtt->publications.clear();
    testRadio.connected = false;
    CHECK(!offCallback("off volet"));
    CHECK(d->getStatus() == UNDEFINED);
    CHECK(published("volet/tele/STATE") == "{\"POWER\":\"None\"}");
    CHECK(published("volet/tele/DETAIL").find("\"state_source\":\"unknown\"") != std::string::npos);

    freshDevice();
    d->setMode(DIMMER);
    d->setStatus(ON);
    d->setBrightness(BRIGHTNESS_MAX);
    g_mqtt->publications.clear();
    testRadio.connected = false;
    CHECK(!offCallback("off volet"));
    CHECK(d->getStatus() == UNDEFINED);
    CHECK(published("volet/tele/STATE") == "{\"POWER\":\"UNDEFINED\"}");
    CHECK(published("volet/tele/BRIGHTNESS").empty());
    freshDevice();
    IrqManager::irqType = E2BP;
}

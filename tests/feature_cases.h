#pragma once

void testTraceIntegration() {
    const byte savedFlags = g_ConfigFlags;
    for (unsigned level = 0; level < 3; ++level) {
        freshDevice();
        CHECK(Yokis::setDebugLevel(g_ConfigFlags, level));
        Serial.out.clear();
        pollReply(g_devices[0], 0, 1);
        CHECK((Serial.out.find("Received:") != std::string::npos) == (level >= 1));
        CHECK((Serial.out.find("Payload:") != std::string::npos) == (level == 2));
        CHECK(g_devices[0]->getStatus() == SHUTTER_CLOSING);
        CHECK(testRadio.writes == 1);
        CHECK(testRadio.lastPayload == std::vector<uint8_t>({0,0,0,0,0,0,127,0,0}));
        Serial.out.clear();
        pollForStatus(g_devices[0]);
        CHECK((Serial.out.find("Failed to check device") != std::string::npos) == (level >= 1));
    }
    g_ConfigFlags = savedFlags;
    freshDevice();
}

void testDiagnosticHttp() {
    const unsigned writes = testRadio.writes;
    for (const char* route : {"/", "/config", "/api/status"}) {
        AsyncWebServerRequest anonymous;
        webserver.dispatch(route, HTTP_GET, &anonymous);
        CHECK(anonymous.status == 401);
    }
    AsyncWebServerRequest page; authorizeAdmin(page);
    webserver.dispatch("/", HTTP_GET, &page);
    CHECK(page.status == 200);
    CHECK(page.response.find("/api/status") != std::string::npos);
    CHECK(page.response.find("innerHTML") == std::string::npos);
    AsyncWebServerRequest config; authorizeAdmin(config);
    webserver.dispatch("/config", HTTP_GET, &config);
    CHECK(config.status == 200);
    CHECK(config.response.find("csrf_token") != std::string::npos);
    AsyncWebServerRequest data; authorizeAdmin(data);
    webserver.dispatch("/api/status", HTTP_GET, &data);
    CHECK(data.status == 200);
    CHECK(data.response.find("\"build\":\"native-test\"") != std::string::npos);
    CHECK(data.response.find("native-http-secret-value") == std::string::npos);
    CHECK(data.response.find("native-ota-secret-value") == std::string::npos);
    for (const char* bad : {"-1", "8", "x", "1junk", "99999999999999"}) {
        AsyncWebServerRequest invalid; authorizeAdmin(invalid);
        invalid.params["page"] = AsyncWebParameter(bad);
        webserver.dispatch("/api/status", HTTP_GET, &invalid);
        CHECK(invalid.status == 400);
    }
    CHECK(testRadio.writes == writes);
}

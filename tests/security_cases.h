// Exercise the real configuration handlers; secret markers are test fixtures.
void testAdminSecurity() {
    const FakeWiFi savedWifi = WiFi;
    WiFi.psk_ = "fixture-wifi-secret";
    CHECK(g_mqtt->setConnectionInfo("broker", 1883, "user", "fixture-mqtt-secret", false));
    CHECK(WebServer::processor("WIFI_PASSWORD").length() == 0);
    CHECK(WebServer::processor("MQTT_PASSWORD").length() == 0);
    CHECK(std::string(html_config_form).find(".innerHTML") == std::string::npos);
    FakeLog diagnostics;
    g_mqtt->printDebug(diagnostics);
    CHECK(diagnostics.out.find("fixture-mqtt-secret") == std::string::npos);
    AsyncWebServerRequest anonymous;
    webserver.dispatch("/", HTTP_GET, &anonymous);
    CHECK(anonymous.status == 401 || anonymous.status == 503);

    CHECK(!Yokis::validAdminSecret(""));
    CHECK(!Yokis::validAdminSecret("short"));
    CHECK(!Yokis::validAdminSecret("<SET_PASSWORD>"));
    CHECK(Yokis::validAdminSecret("fixture-admin-secret"));
    FakeOTA ota;
    CHECK(!Yokis::configureAdminOta(ota, ""));
    CHECK(ota.beginCalls == 0);
    CHECK(Yokis::configureAdminOta(ota, "fixture-ota-secret"));
    CHECK(ota.beginCalls == 1 && ota.password == "fixture-ota-secret");

    AsyncWebServerRequest root;authorizeAdmin(root);
    webserver.dispatch("/", HTTP_GET, &root);
    CHECK(root.status == 200);
    CHECK(root.response.find("fixture-wifi-secret") == std::string::npos);
    CHECK(root.response.find("fixture-mqtt-secret") == std::string::npos);
    CHECK(WebServer::processor("CSRF_TOKEN").length() == 32);

    AsyncWebServerRequest get;authorizeAdmin(get);get.params["mqtt_port"]=AsyncWebParameter("1999");
    webserver.dispatch("/save_config", HTTP_GET, &get);CHECK(get.status == 405);
    AsyncWebServerRequest missing;missing.authUser=YOKIS_HTTP_USER;missing.authPassword=YOKIS_HTTP_PASSWORD;
    missing.bodyParams["mqtt_port"]=AsyncWebParameter("1999");
    webserver.dispatch("/save_config",HTTP_POST,&missing);CHECK(missing.status == 403);
    AsyncWebServerRequest wrong;authorizeAdmin(wrong);wrong.bodyParams["csrf_token"]=AsyncWebParameter("wrong");
    webserver.dispatch("/save_config",HTTP_POST,&wrong);CHECK(wrong.status == 403);

    AsyncWebServerRequest keep;authorizeAdmin(keep);keep.bodyParams["mqtt_port"]=AsyncWebParameter("1885");keep.bodyParams["mqtt_password"]=AsyncWebParameter("");
    webserver.dispatch("/save_config",HTTP_POST,&keep);CHECK(keep.status == 202);
    testClockUs += 200000;webserver.processPending();
    CHECK(std::string(g_mqtt->getPassword()) == "fixture-mqtt-secret");
    AsyncWebServerRequest conflict;authorizeAdmin(conflict);conflict.bodyParams["mqtt_password_clear"]=AsyncWebParameter("1");conflict.bodyParams["mqtt_password"]=AsyncWebParameter("new-secret");
    webserver.dispatch("/save_config",HTTP_POST,&conflict);CHECK(conflict.status == 400);
    CHECK(std::string(g_mqtt->getPassword()) == "fixture-mqtt-secret");
    AsyncWebServerRequest clear;authorizeAdmin(clear);clear.bodyParams["mqtt_password_clear"]=AsyncWebParameter("1");
    webserver.dispatch("/save_config",HTTP_POST,&clear);CHECK(clear.status == 202);
    testClockUs += 200000;webserver.processPending();CHECK(std::string(g_mqtt->getPassword()).empty());

    WebServer disabled(81,"admin","");disabled.begin();
    AsyncWebServerRequest off;disabled.dispatch("/",HTTP_GET,&off);
    CHECK(off.status == 503);CHECK(!disabled.started);
    WiFi.mode_ = WIFI_STA;
    setupWifiAP("");CHECK(WiFi.getMode() == WIFI_STA);
    setupWifiAP("fixture-protected-ap");CHECK(WiFi.apPassword == "fixture-protected-ap");
    WiFi = savedWifi;
}

// Production MQTT and Wi-Fi calls. No discovery or reconnection logic is duplicated here.
void testNetwork(Device* d, WiFiClient& wifi) {
 freshDevice();g_ConfigFlags=0;IrqManager::irqType=E2BP;
 g_mqtt->connected_=true;g_mqtt->publications.clear();
 d->offline();CHECK(g_mqtt->publishDevice(d));
 CHECK(published("volet/tele/LWT")=="Offline");
 CHECK(d->isOffline());
 std::string config=published("homeassistant/cover/volet/config");
 CHECK(config.find("\"avty_mode\":\"all\"")!=std::string::npos);
 CHECK(config.find("\"avty\":[")!=std::string::npos);
 CHECK(config.find("\"avty_t\":")==std::string::npos);
 // Unknown POSITION is still independent of RF availability.
 d->online();d->setStatus(UNDEFINED);g_mqtt->publishDevice(d);
 CHECK(published("volet/tele/LWT")=="Online");
 CHECK(g_mqtt->setConnectionInfo("broker",1883,"","",false));
 g_mqtt->connections.clear();WiFi.status_=WL_CONNECTED;
 CHECK(g_mqtt->reconnect(true));CHECK(g_mqtt->connections.size()==1);
 auto first=g_mqtt->connections.back();
 CHECK(!first.willTopic.empty());CHECK(first.willMessage=="Offline");CHECK(first.retained);CHECK(first.qos==1);
 CHECK(published(first.willTopic.c_str())=="Online");
 g_mqtt->events.clear();g_mqtt->disconnect();
 CHECK(g_mqtt->events.size()>=2&&g_mqtt->events[0]=="publish:Offline");
 CHECK(published(first.willTopic.c_str())=="Offline");
 CHECK(g_mqtt->reconnect(true));CHECK(g_mqtt->connections.back().id==first.id);
 CHECK(g_mqtt->connections.back().willTopic==first.willTopic);
 // No network connection attempt while Wi-Fi is absent.
 g_mqtt->connected_=false;WiFi.status_=WL_DISCONNECTED;
 size_t count=g_mqtt->connections.size();CHECK(!g_mqtt->reconnect(true));CHECK(g_mqtt->connections.size()==count);
 WiFi.status_=WL_CONNECTED;g_mqtt->connected_=true;
 // Birth messages are consumed before the RF parser; no radio emission.
 g_mqtt->setDiscoveryDone(true);Serial.out.clear();unsigned writes=testRadio.writes;
 deliver("homeassistant/status","online");
 CHECK(!g_mqtt->isDiscoveryDone());CHECK(testRadio.writes==writes);
 CHECK(Serial.out.find("invalid topic or payload")==std::string::npos);
 g_mqtt->setDiscoveryDone(true);deliver("homeassistant/status","offline");CHECK(g_mqtt->isDiscoveryDone());
 // Each supported mode with a maximum name fits the configured MQTT packet.
 for (DeviceMode mode : {ON_OFF, DIMMER, SHUTTER, NO_RCPT}) {
  Device longest(std::string(48,'n').c_str(),address,1);longest.setMode(mode);
  CHECK(g_mqtt->publishDevice(&longest));
  auto t=std::string("homeassistant/")+(mode==SHUTTER?"cover/":"light/")+longest.getName()+"/config";
  const auto json=published(t.c_str());CHECK(!json.empty());
  CHECK(json.size()+t.size()+7<=1024);
  std::ofstream("tests/build/network-discovery-"+std::to_string(int(mode))+".json")<<json;
 }
 // Birth online is coalesced while refresh is in progress; one device per tick.
 freshDevice();g_ConfigFlags=0;g_mqtt->connected_=true;
 Device* second=new Device("network2",address,2);second->setMode(SHUTTER);second->offline();g_devices[1]=second;
 g_mqtt->publications.clear();g_mqtt->subscriptions.clear();g_mqtt->setDiscoveryDone(false);
 const uint32_t stamp=d->getLastUpdateMillis();testRadio=RadioFixture();
 g_mqtt->serviceRefresh(g_devices,64);
 CHECK(!g_mqtt->isDiscoveryDone());CHECK(testRadio.writes==0);
 CHECK(published("homeassistant/cover/volet/config")!="");
 CHECK(published("homeassistant/cover/network2/config")=="");
 deliver("homeassistant/status","online"); // does not reset progress or subscribe again
 testClockUs+=100000;g_mqtt->serviceRefresh(g_devices,64);
 CHECK(g_mqtt->isDiscoveryDone());CHECK(published("network2/tele/LWT")=="Offline");
 CHECK(g_mqtt->refreshPending());CHECK(testRadio.writes==0);
 testClockUs+=100000;g_mqtt->serviceRefresh(g_devices,64);
 CHECK(published("volet/tele/DETAIL").find("\"state_replayed\":true")!=std::string::npos);
 CHECK(d->getLastUpdateMillis()==stamp);CHECK(g_mqtt->refreshPending());
 testClockUs+=100000;g_mqtt->serviceRefresh(g_devices,64);
 CHECK(!g_mqtt->refreshPending());CHECK(testRadio.writes==0);CHECK(second->isOffline());
 CHECK(d->needsPolling());CHECK(second->needsPolling());
 CHECK(std::count(g_mqtt->subscriptions.begin(),g_mqtt->subscriptions.end(),"homeassistant/status")<=1);
 // No repeated replay after completion and no state age reset on replay.
 size_t pubs=g_mqtt->publications.size();testClockUs+=200000;g_mqtt->serviceRefresh(g_devices,64);
 CHECK(g_mqtt->publications.size()==pubs);CHECK(d->getLastUpdateMillis()==stamp);
 // A partial/malformed birth cannot restart a completed cycle or enter RF.
 for (auto payload : {"on", "onlineX", "ON", "", "offline"}) deliver("homeassistant/status",payload);
 CHECK(g_mqtt->isDiscoveryDone());CHECK(testRadio.writes==0);
 uint8_t embedded[]={ 'o','n','l','i','n','e',0,'x' };char birth[]="homeassistant/status";
 mqttCallback(birth,embedded,sizeof(embedded));CHECK(g_mqtt->isDiscoveryDone());
 mqttCallback(birth,nullptr,6);CHECK(g_mqtt->isDiscoveryDone());
 // Commands in the receive buffer take priority over a requested refresh.
 g_mqtt->setCallback(mqttCallback);g_mqtt->setDiscoveryDone(false);g_mqtt->publications.clear();
 wifi.packets.push_back({"volet/cmnd/POWER","OFF"});testRadio.replies.push_back({0,0});loop();
 CHECK(testRadio.writes==1);CHECK(published("homeassistant/cover/volet/config")=="");
 // A STOP check also runs before re-discovery; no loss of the old stop context.
 g_ConfigFlags=FLAG_POLLING;testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=200000;testRadio.replies.push_back({0,0});writes=testRadio.writes;loop();
 CHECK(testRadio.writes==writes+1);CHECK(d->getStatus()==SHUTTER_STOPPED);
 CHECK(published("homeassistant/cover/volet/config")=="");
 g_ConfigFlags=0;g_devices[1]=nullptr;delete second;
 // A publication failure leaves refresh pending and can be retried.
 g_mqtt->setDiscoveryDone(false);g_mqtt->failPublish=true;g_mqtt->serviceRefresh(g_devices,64);
 CHECK(!g_mqtt->isDiscoveryDone());g_mqtt->failPublish=false;
 testClockUs+=200000;g_mqtt->serviceRefresh(g_devices,64);CHECK(g_mqtt->isDiscoveryDone());
 testClockUs+=200000;g_mqtt->failPublish=true;g_mqtt->serviceRefresh(g_devices,64);CHECK(g_mqtt->refreshPending());
 testClockUs+=200000;g_mqtt->failPublish=false;g_mqtt->serviceRefresh(g_devices,64);CHECK(!g_mqtt->refreshPending());
 // Failed Online announcement is retried after connect, even with no RF polling.
 g_mqtt->disconnect();g_mqtt->publications.clear();g_mqtt->failPublish=true;
 CHECK(g_mqtt->reconnect(true));CHECK(published(first.willTopic.c_str())=="");
 g_mqtt->failPublish=false;testClockUs+=500000;g_mqtt->loop();
 CHECK(published(first.willTopic.c_str())=="Online");
 // Failed Offline publish must not send MQTT DISCONNECT (which cancels the will).
 g_mqtt->failPublish=true;unsigned clean=g_mqtt->disconnectCount;wifi.stopped=false;g_mqtt->disconnect();
 CHECK(wifi.stopped);CHECK(g_mqtt->disconnectCount==clean);
 g_mqtt->failPublish=false;g_mqtt->connected_=false;wifi.stopped=false;CHECK(g_mqtt->reconnect(true));
 // Clean restart announces Offline before restarting the microcontroller.
 g_mqtt->events.clear();unsigned reboot=ESP.restartCount;restart(nullptr);
 CHECK(ESP.restartCount==reboot+1);CHECK(published(first.willTopic.c_str())=="Offline");
 // Polling state replay at wrap-around is still bounded and does not mutate state.
 testClockUs=(uint64_t(UINT32_MAX)-20)*1000;g_mqtt->connected_=true;g_mqtt->setDiscoveryDone(false);
 g_mqtt->serviceRefresh(g_devices,64);CHECK(g_mqtt->isDiscoveryDone());
 testClockUs+=200000;g_mqtt->serviceRefresh(g_devices,64);CHECK(!g_mqtt->refreshPending());
 // Distinct hardware has a distinct stable gateway identity.
 {WiFiClient otherWifi;ESP.chipId=0x654321;Mqtt otherMqtt(otherWifi);
  CHECK(std::string(otherMqtt.clientId())!=g_mqtt->clientId());
  CHECK(std::string(otherMqtt.gatewayTopic())!=g_mqtt->gatewayTopic());ESP.chipId=0x123456;}
 // Retained states are explicitly removed with an entity while connected.
 g_mqtt->removeDiscovery(d);CHECK(published("volet/tele/STATE")=="");CHECK(published("volet/tele/DETAIL")=="");
 // Re-entering the same Wi-Fi credentials must reconnect rather than do nothing.
 WiFi=FakeWiFi();WiFi.ssid="home";WiFi.psk_="password";
 WiFi.savedSsid="home";WiFi.savedPsk="password";WiFi.status_=WL_DISCONNECTED;
 setupWifi("home","password");CHECK(WiFi.status()==WL_CONNECTED);CHECK(WiFi.reconnectCalls>0||WiFi.beginCalls>0);
 CHECK(WiFi.flashWrites==0);CHECK(WiFi.savedSsid=="home");CHECK(!WiFi.persistent_);
 WiFi.mode_=WIFI_OFF;WiFi.status_=WL_DISCONNECTED;
 setupWifi("home","password");CHECK(WiFi.getMode()==WIFI_STA);CHECK(WiFi.status()==WL_CONNECTED);CHECK(WiFi.flashWrites==0);
 // Deliberate changes persist, but subsequent reconnects must not erase/write flash.
 setupWifi("new-home","new-password");CHECK(WiFi.savedSsid=="new-home");CHECK(WiFi.savedPsk=="new-password");CHECK(!WiFi.persistent_);
 int fw=WiFi.flashWrites;WiFi.status_=WL_DISCONNECTED;CHECK(wifiReconnect("wifiReconnect"));
 CHECK(WiFi.flashWrites==fw);CHECK(WiFi.savedSsid=="new-home");CHECK(!WiFi.persistent_);
 WiFi.apReachable=false;WiFi.status_=WL_DISCONNECTED;setupWifi();
 CHECK(WiFi.savedSsid=="new-home");CHECK(WiFi.getMode()==WIFI_STA);
 // AP fallback must not erase saved station data.
 setupWifiAP();CHECK(WiFi.savedSsid=="new-home");CHECK(!WiFi.persistent_);
 CHECK(resetWifiConfig());CHECK(WiFi.savedSsid.length()==0);CHECK(WiFi.savedPsk.length()==0);CHECK(!WiFi.persistent_);
 WiFi=FakeWiFi();g_mqtt->connected_=true;freshDevice();g_ConfigFlags=FLAG_POLLING;
}

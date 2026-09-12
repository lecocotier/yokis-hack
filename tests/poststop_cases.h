// Integration checks of production polling / E2bp / feedback code.
// No scheduler or decoder is reimplemented here.
void testPostStopPolling(Device* d, WiFiClient& wifi) {
 g_ConfigFlags=FLAG_POLLING;IrqManager::irqType=E2BP;
 g_mqtt->setConnectionInfo("broker",1883,"","",false);
 g_mqtt->connected_=true;g_mqtt->setDiscoveryDone(true);
 g_mqtt->setCallback(mqttCallback);
 freshDevice();
 testRadio.replies.push_back({0,1});command("PAUSE");
 unsigned before=testRadio.writes;
 testClockUs+=200000;testRadio.replies.push_back({0,0});loop();
 CHECK(testRadio.writes==before+1); // no periodic timer was requested
 CHECK(testRadio.lastPayload[0]==0); // actual status query, not another STOP
 CHECK(d->getStatus()==SHUTTER_STOPPED);
 CHECK(published("volet/tele/DETAIL").find("stopped_observed")!=std::string::npos);
 // The stop context must not disappear merely because polling was delayed.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=6000000;pollReply(d,0,0);
 CHECK(d->getStatus()==UNDEFINED);
 // Two STOP packets already received must run before the first status query.
 const uint8_t addr2[]={3,4,3,4,4};
 Device* other=new Device("poststop2",addr2,48);other->setMode(SHUTTER);g_devices[1]=other;
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=200000;d->pollMePlease();
 wifi.packets.push_back({"poststop2/cmnd/POWER","PAUSE"});
 wifi.packets.push_back({"volet/cmnd/POWER","PAUSE"});
 testRadio.replies.push_back({0,1});testRadio.replies.push_back({0,1});
 before=testRadio.writes;loop();
 CHECK(testRadio.writes==before+1);
 CHECK(testRadio.destinations.back().first==48);
 CHECK(testRadio.lastPayload[0]==0x1a);
 loop();CHECK(testRadio.writes==before+2);
 CHECK(testRadio.lastPayload[0]==0x1a);
 wifi.packets.clear();g_devices[1]=nullptr;delete other;
 // An ordinary poll must not occupy the radio during the 100ms first wait.
 other=new Device("poststop2",addr2,48);other->setMode(SHUTTER);g_devices[1]=other;
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 other->pollMePlease();before=testRadio.writes;
 testClockUs+=50000;loop();CHECK(testRadio.writes==before);
 testClockUs+=100000;testRadio.replies.push_back({0,0});loop();
 CHECK(testRadio.lastPayload[0]==0);CHECK(testRadio.destinations.back().first==47);
 CHECK(other->needsPolling()); // normal work remains pending, not discarded
 testRadio.replies.push_back({0x10,2});loop();CHECK(!other->needsPolling());
 g_devices[1]=nullptr;delete other;
 // Earliest attempt uses the end of the command, with no blocking delay.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 CHECK(d->shutterFeedback().verification().pending());
 CHECK(d->shutterFeedback().verification().attempts()==0);
 uint64_t done=testClockUs;before=testRadio.writes;
 testClockUs=done+99000;loop();CHECK(testRadio.writes==before);
 testClockUs=done+100000;testRadio.replies.push_back({0,1});loop();
 CHECK(testRadio.writes==before+1);
 CHECK(d->shutterFeedback().verification().pending());
 CHECK(d->shutterFeedback().verification().attempts()==1);
 CHECK(d->getStatus()==SHUTTER_CLOSING); // actual motion remains visible
 uint64_t firstDone=testClockUs;
 d->pollMePlease(); // ordinary timer must not bypass verification backoff
 testClockUs=firstDone+240000;loop();CHECK(testRadio.writes==before+1);
 testClockUs=firstDone+250000;testRadio.replies.push_back({0,0});loop();
 CHECK(testRadio.writes==before+2);
 CHECK(d->shutterFeedback().verification().attempts()==2);
 CHECK(!d->shutterFeedback().verification().pending());
 CHECK(!d->needsPolling()); // same observation satisfies the old periodic request
 CHECK(d->getStatus()==SHUTTER_STOPPED);
 testClockUs+=1000000;loop();CHECK(testRadio.writes==before+2);

 // Missing replies: bounded 3 attempts, no fourth, no rapid false Offline.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 before=testRadio.writes;
 for(unsigned attempt=1;attempt<=3;++attempt){
  testClockUs+=300000;loop();
  CHECK(d->shutterFeedback().verification().attempts()==attempt);
  CHECK(testRadio.writes==before+attempt);
  CHECK(d->isOnline());CHECK(d->getFailedPollings()==0);
 }
 CHECK(std::string(d->shutterFeedback().verification().name())=="inconclusive");
 CHECK(d->getStatus()==UNDEFINED);
 testClockUs+=1000000;loop();CHECK(testRadio.writes==before+3);
 pollReply(d,0,0);CHECK(d->getStatus()==UNDEFINED); // no false endpoint after expiry
 testRadio.replies.push_back({0,0});command("OFF");pollReply(d,0,0);
 CHECK(d->getStatus()==SHUTTER_CLOSED); // new command establishes fresh context

 // Three successful RXs containing motion are NOT a verified STOP.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 for(unsigned attempt=1;attempt<=3;++attempt){
  testClockUs+=300000;testRadio.replies.push_back({0,1});loop();
 }
 CHECK(std::string(d->shutterFeedback().verification().name())=="inconclusive");
 CHECK(d->getStatus()==SHUTTER_CLOSING);
 pollReply(d,0,0);CHECK(d->getStatus()==UNDEFINED);

 // A failed STOP still requests a check, but does not acquire an ACK by inference.
 freshDevice();command("PAUSE");before=testRadio.writes;
 CHECK(d->shutterFeedback().verification().pending());
 testClockUs+=200000;testRadio.replies.push_back({0,0});loop();
 CHECK(testRadio.writes==before+1);CHECK(!d->shutterFeedback().commandResponse());
 CHECK(d->getStatus()==UNDEFINED);

 // A rich endpoint resolves the check without rewriting the historical decoder.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=200000;testRadio.replies.push_back({0x10,2});loop();
 CHECK(d->getStatus()==SHUTTER_CLOSED);
 CHECK(std::string(d->shutterFeedback().verification().name())=="endpoint_observed");
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=200000;testRadio.replies.push_back({4,2});loop();
 CHECK(d->getStatus()==SHUTTER_STOPPED);
 CHECK(std::string(d->shutterFeedback().verification().name())=="stopped_observed");

 // Superseding movement always cancels a pending STOP check, even on RF init failure.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testRadio.replies.push_back({0,0});command("ON");before=testRadio.writes;
 testClockUs+=300000;loop();CHECK(testRadio.writes==before);
 CHECK(std::string(d->shutterFeedback().verification().name())=="none");
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testRadio.connected=false;command("OFF");CHECK(!d->shutterFeedback().verification().pending());
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testRadio.connected=false;CHECK(!g_bp->toggle());CHECK(!d->shutterFeedback().verification().pending());

 // Low-level console movement must not leave an old automatic STOP check armed.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testRadio.replies.push_back({0,0});CHECK(pressCallback("press volet"));
 CHECK(!d->shutterFeedback().verification().pending());
 CHECK(std::string(d->shutterFeedback().verification().name())=="none");
 CHECK(std::string(d->shutterFeedback().rawOrigin())=="command_reply");

 // Poll OFF and scanner ownership suppress automatic checks, but expire honestly.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 before=testRadio.writes;g_ConfigFlags=0;testClockUs+=200000;loop();
 CHECK(testRadio.writes==before);CHECK(d->shutterFeedback().verification().pending());
 testClockUs+=6000000;loop();CHECK(testRadio.writes==before);
 CHECK(d->getStatus()==UNDEFINED);
 CHECK(std::string(d->shutterFeedback().verification().name())=="inconclusive");
 g_ConfigFlags=FLAG_POLLING;
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 before=testRadio.writes;IrqManager::irqType=SCANNER;testClockUs+=200000;loop();
 CHECK(testRadio.writes==before);CHECK(IrqManager::irqType==SCANNER);
 IrqManager::irqType=E2BP;testRadio.replies.push_back({0,0});loop();CHECK(testRadio.writes==before+1);

 // A serial STOP must be fully read before any status query is inserted.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=200000;before=testRadio.writes;
 testRadio.replies.push_back({0,1});Serial.feed("pause volet\n");
 while(Serial.available())loop();
 CHECK(testRadio.writes==before+1);CHECK(testRadio.lastPayload[0]==0x1a);

 // The round-robin urgent selector handles 2 devices without mixing contexts.
 other=new Device("poststop2",addr2,48);other->setMode(SHUTTER);g_devices[1]=other;
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testRadio.replies.push_back({1,1});deliver("poststop2/cmnd/POWER","PAUSE");
 testClockUs+=200000;before=testRadio.writes;
 testRadio.replies.push_back({4,2});testRadio.replies.push_back({4,2});
 loop();loop();CHECK(testRadio.writes==before+2);
 CHECK(!d->shutterFeedback().verification().pending());
 CHECK(!other->shutterFeedback().verification().pending());
 CHECK(testRadio.destinations[testRadio.destinations.size()-2].first!=testRadio.destinations.back().first);
 g_devices[1]=nullptr;delete other;

 // Rollover and reconstruction preserve neither stale pointers nor old STOPs.
 freshDevice();testClockUs=(uint64_t(UINT32_MAX)-50)*1000;
 testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=200000;testRadio.replies.push_back({0,0});loop();
 CHECK(d->getStatus()==SHUTTER_STOPPED);
 CHECK(d->shutterFeedback().verification().attempts()==1);
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 freshDevice();before=testRadio.writes;testClockUs+=300000;loop();
 CHECK(testRadio.writes==before);CHECK(!d->shutterFeedback().verification().pending());
 freshDevice();
}

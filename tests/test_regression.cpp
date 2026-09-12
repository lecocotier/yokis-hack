#include "globals.h"
#include "commands/callbacks.h"
#include "reliability.h"
#include "net/wifi.h"
#include <iostream>
#include <new>
#include <fstream>

FakeLog Serial; FakeESP ESP; FakeFsState testFs; FakeLittleFS LittleFS; FakeWiFi WiFi;
uint64_t testClockUs=10000000; std::function<void()> testYield; RadioFixture testRadio;
FakeOTA ArduinoOTA; WebServer webserver(80);
byte g_ConfigFlags=FLAG_POLLING;
Device* g_devices[MQTT_MAX_NUM_OF_YOKIS_DEVICES]={};
Ticker* g_deviceStatusPollers[MQTT_MAX_NUM_OF_YOKIS_DEVICES]={};
E2bp* g_bp=nullptr;Pairing* g_pairingRF=nullptr;Scanner* g_scanner=nullptr;Copy* g_copy=nullptr;
Device* g_currentDevice=nullptr;MqttHass* g_mqtt=nullptr;SerialHelper* g_serial=nullptr;
volatile IrqType IrqManager::irqType=E2BP;
static int checks=0,failures=0;
#define CHECK(c) do{++checks;if(!(c)){++failures;std::cerr<<"FAIL "<<__LINE__<<": "<<#c<<"\n";}}while(0)
const uint8_t address[]={1,2,1,2,2};
void deliver(const char* topic,const char* payload){
 std::vector<char> t(topic,topic+strlen(topic)+1);
 mqttCallback(t.data(),(uint8_t*)payload,strlen(payload));
}
void command(const char* payload){deliver("volet/cmnd/POWER",payload);}
void freshDevice(){
 Device fresh("volet",address,47);fresh.setMode(SHUTTER);g_devices[0]->copy(&fresh);
 g_bp->setDevice(g_devices[0]);testRadio=RadioFixture();testClockUs=10000000;
 Serial.out.clear();
}
void drainSerial(){while(Serial.available())g_serial->readFromSerial();}
// Production API only: no rewritten decoder in these regression tests.
std::string published(const char* topic) {
 for (auto it=g_mqtt->publications.rbegin();it!=g_mqtt->publications.rend();++it)
  if(it->first==topic)return it->second;
 return "";
}
void pollReply(Device* d,uint8_t a,uint8_t b){testRadio.replies.push_back({a,b});pollForStatus(d);}
#include "poststop_cases.h"
#include "network_cases.h"
int main(){
 // A mount failure must not autoformat stored RF/MQTT parameters.
 testFs.files["/keep.conf"]=std::make_shared<std::string>("pairing backup");
 testFs.failMount=true;
 CHECK(!YokisLittleFS::init());CHECK(testFs.files.count("/keep.conf")==1);
 testFs.failMount=false;
 WiFiClient wifi;
 g_mqtt=new MqttHass(wifi);g_bp=new E2bp(1,2);
 g_pairingRF=new Pairing(1,2);g_scanner=new Scanner(1,2);g_copy=new Copy(1,2);
 g_serial=new SerialHelper();registerAllCallbacks();g_currentDevice=new Device("tempDevice");
 g_devices[0]=new Device("volet",address,47);
 testYield=[](){if(!testRadio.fifo.empty())g_bp->interruptRxReady();};

 { E2bp empty(1,2); CHECK(!empty.press()); CHECK(!empty.release()); }
 freshDevice();Device* d=g_devices[0];
 d->setStatus(SHUTTER_OPENED);testClockUs+=50000;
 testRadio.replies.push_back({0,1});command("OFF");
 CHECK(testRadio.writes==1);CHECK(d->getStatus()==SHUTTER_CLOSING);
 CHECK(testRadio.lastPayload==std::vector<uint8_t>({0xfa,6,0,0,0,0,127,0,0}));
 // All shutter orders, even equal orders, remain admissible at close intervals.
 testRadio.replies.push_back({0,1});command("OFF");CHECK(testRadio.writes==2);
 testRadio.replies.push_back({15,2});command("PAUSE");CHECK(testRadio.writes==3);
 CHECK(d->getStatus()==SHUTTER_STOPPED);CHECK(testRadio.lastPayload[0]==0x1a);
 testRadio.replies.push_back({15,2});command("PAUSE");CHECK(testRadio.writes==4);
 testRadio.replies.push_back({1,1});command("ON");CHECK(testRadio.writes==5);
 CHECK(testRadio.lastPayload[0]==0xb9);CHECK(d->getStatus()==SHUTTER_OPENING);

 // Rejected syntax/unknown devices must not access the radio or crash.
 unsigned before=testRadio.writes;
 const char* badTopics[]={"", "volet", "volet/cmnd", "volet/cmnd/", "volet/cmnd/POWER/x",
  "/volet/cmnd/POWER", "volet/other/POWER", "missing/cmnd/POWER", "volet/cmnd/UNKNOWN"};
 for(auto t:badTopics){deliver(t,"ON");CHECK(testRadio.writes==before);}
 const char* badPayloads[]={"", "on", "OPEN", "ONjunk", "256", "-1", "ON OFF"};
 for(auto p:badPayloads){command(p);CHECK(testRadio.writes==before);}
 deliver("volet/cmnd/BRIGHTNESS","1");CHECK(testRadio.writes==before);
 uint8_t bytes[]={0,'O','N'};char topic[]="volet/cmnd/POWER";
 mqttCallback(topic,bytes,sizeof(bytes));CHECK(testRadio.writes==before);
 mqttCallback(nullptr,bytes,1);mqttCallback(topic,nullptr,1);CHECK(testRadio.writes==before);

 freshDevice();testRadio.replies.clear();command("OFF");
 CHECK(!g_bp->hasResponse());CHECK(d->getStatus()==UNDEFINED);
 before=testRadio.writes;testRadio.replies.push_back({0,1});command("OFF");
 CHECK(testRadio.writes>before);CHECK(g_bp->hasResponse());
 freshDevice();testRadio.connected=false;command("ON");CHECK(testRadio.writes==0);CHECK(d->getStatus()==UNDEFINED);

 // A decoded state and reachability are separate facts.
 freshDevice();testRadio.replies.push_back({0x25,0x42});pollForStatus(d);
 CHECK(g_bp->hasResponse());CHECK(d->isOnline());CHECK(d->getFailedPollings()==0);CHECK(d->getStatus()==UNDEFINED);
 const uint8_t replies[][2]={{0x2f,2},{0x1e,2},{1,1},{0,1},{0xf,2},{0,0},{1,0}};
 const DeviceStatus expected[]={SHUTTER_OPENED,SHUTTER_CLOSED,SHUTTER_OPENING,SHUTTER_CLOSING,SHUTTER_STOPPED,SHUTTER_CLOSED,SHUTTER_OPENED};
 for(unsigned i=0;i<7;++i){freshDevice();testRadio.replies.push_back({replies[i][0],replies[i][1]});pollForStatus(d);CHECK(d->getStatus()==expected[i]);}

 // User trace: complete descent and partial descent share the same 00 00.
 // Reintroduce the original 5-second STOP context, not a false raw end switch.
 freshDevice();pollReply(d,1,0);CHECK(d->getStatus()==SHUTTER_OPENED);
 testRadio.replies.push_back({1,0});command("OFF");
 pollReply(d,0,1);CHECK(d->getStatus()==SHUTTER_CLOSING);
 testRadio.replies.push_back({0,1});command("PAUSE");
 pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 testClockUs+=12000000;pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 CHECK(published("volet/tele/DETAIL").find("command_stop_estimate")!=std::string::npos);
 CHECK(published("volet/tele/STATE")=="{\"POWER\":\"stopped\"}");
 // A new directional order must clear the old stop, even if a poll missed motion.
 testRadio.replies.push_back({0,0});command("OFF");
 pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_CLOSED);
 CHECK(published("volet/tele/DETAIL").find("simple_rf_estimate")!=std::string::npos);
 // Mirror path: partial opening/STOP then an opening allowed to complete.
 testRadio.replies.push_back({0,0});command("ON");pollReply(d,1,1);
 testRadio.replies.push_back({1,0});command("PAUSE");
 pollReply(d,1,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 testClockUs+=12000000;pollReply(d,1,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 testRadio.replies.push_back({1,0});command("ON");
 pollReply(d,1,0);CHECK(d->getStatus()==SHUTTER_OPENED);
 // Observed motion from an external control also clears the latched STOP.
 testRadio.replies.push_back({1,0});command("PAUSE");pollReply(d,1,0);
 pollReply(d,0,1);pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_CLOSED);
 // A deferred verification must expire to UNKNOWN, never a false endpoint.
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=4000000;pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 freshDevice();testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=6000000;pollReply(d,0,0);CHECK(d->getStatus()==UNDEFINED);
 // No RX is not a successful STOP, nor proof of the endpoint on the next poll.
 freshDevice();command("PAUSE");pollReply(d,0,0);CHECK(d->getStatus()==UNDEFINED);
 pollReply(d,0,1);pollReply(d,0,0);CHECK(d->getStatus()==UNDEFINED);
 // Motion observed after the inconclusive campaign can restore an estimate.
 pollReply(d,0,1);pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_CLOSED);
 // Rich status bytes keep precedence over the estimation.
 testRadio.replies.push_back({0,0});command("PAUSE");
 pollReply(d,0x10,2);CHECK(d->getStatus()==SHUTTER_CLOSED);
 CHECK(published("volet/tele/DETAIL").find("rf_status")!=std::string::npos);
 // Time arithmetic remains correct when STOP occurs across rollover.
 freshDevice();testClockUs=(uint64_t(UINT32_MAX)-100)*1000;
 testRadio.replies.push_back({0,1});command("PAUSE");
 testClockUs+=1000000;pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 // Configuration parsing/boot resets volatile context, never restores old STOP.
 freshDevice();testRadio.replies.push_back({0,0});command("PAUSE");
 freshDevice();pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_CLOSED);
 // Several near-simultaneous deliveries: distinct device contexts and addresses.
 const uint8_t secondAddress[]={3,4,3,4,4};
 Device* other=new Device("second",secondAddress,48);other->setMode(SHUTTER);g_devices[1]=other;
 testRadio.replies.push_back({0,0});command("PAUSE");
 for(int i=0;i<8;++i){
  unsigned count=testRadio.writes;
  testRadio.replies.push_back({0,0});deliver("second/cmnd/POWER","OFF");
  CHECK(testRadio.writes==count+1);CHECK(other->getStatus()==SHUTTER_CLOSING);
  CHECK(testRadio.destinations.back().first==48);
  CHECK(testRadio.destinations.back().second==std::vector<uint8_t>(secondAddress,secondAddress+5));
 }
 pollReply(other,0,0);CHECK(other->getStatus()==SHUTTER_CLOSED);
 pollReply(d,0,0);CHECK(d->getStatus()==SHUTTER_STOPPED);
 // A timeout on one device does not suppress the next device's command.
 testRadio.replies.clear();deliver("second/cmnd/POWER","ON");
 unsigned afterTimeout=testRadio.writes;testRadio.replies.push_back({0,0});command("OFF");
 CHECK(testRadio.writes==afterTimeout+1);CHECK(d->getStatus()==SHUTTER_CLOSING);
 CHECK(testRadio.destinations.back().first==47);
 CHECK(testRadio.destinations.back().second==std::vector<uint8_t>(address,address+5));
 g_devices[1]=nullptr;delete other;
 // HA must not reinterpret a stopped descent as a closed endpoint.
 freshDevice();g_mqtt->publishDevice(d);std::string discovery=published("homeassistant/cover/volet/config");
 CHECK(std::string(discovery).find("\"optimistic\":true")!=std::string::npos);
 CHECK(std::string(discovery).find("'None' if value_json.POWER == 'stopped'")!=std::string::npos);
 CHECK(std::string(discovery).find("~tele/DETAIL")!=std::string::npos);
 std::ofstream("tests/build/shutter-discovery.json")<<discovery;
 testRadio.replies.push_back({0,0});command("PAUSE");pollReply(d,0,0);
 std::ofstream("tests/build/shutter-detail.json")<<published("volet/tele/DETAIL");
 {Device longest(std::string(48,'x').c_str(),address,1);longest.setMode(SHUTTER);
  g_mqtt->publishDevice(&longest);std::string maxDiscovery=published(("homeassistant/cover/"+std::string(48,'x')+"/config").c_str());
  CHECK(maxDiscovery.size()+strlen("homeassistant/cover//config")+48+7<MQTT_MAX_PACKET_SIZE);}

 // Verification budget starts at transaction completion, across rollover.
 for(uint32_t delta : {uint32_t(4999),uint32_t(5000)}) {
  Yokis::ShutterFeedback f;f.begin(Yokis::ShutterFeedback::Pause,UINT32_MAX-100);
  CHECK(f.observe(0,1,UINT32_MAX-90)==UNDEFINED); // command reply not a poll
  CHECK(f.finish(true,UINT32_MAX-80)==SHUTTER_STOPPED);
  CHECK(f.observe(0,0,uint32_t(UINT32_MAX-80+delta))==
      (delta<5000 ? SHUTTER_STOPPED : UNDEFINED));
 }

 // Deadlines on both sides of millis rollover; retry no response remains finite.
 freshDevice();testClockUs=(uint64_t(UINT32_MAX)-200)*1000;
 testRadio.replies.push_back({0,1});CHECK(g_bp->off());
 freshDevice();testClockUs=(uint64_t(UINT32_MAX)-200)*1000;
 uint64_t start=testClockUs;CHECK(!g_bp->off());CHECK(testClockUs-start<1600000);CHECK(testRadio.writes>0);
 CHECK(!Yokis::elapsed(5,UINT32_MAX-20,100));CHECK(Yokis::elapsed(100,UINT32_MAX-20,100));

 // Borrowed ownership, safe unconfigured targets, distinct press/release states.
 {E2bp borrowed(1,2);borrowed.setDevice(d);}CHECK(d->isConfigured());
 {Device blank("empty");E2bp radio(1,2);radio.setDevice(&blank);CHECK(!radio.on());}
 freshDevice();d->setMode(ON_OFF);g_bp->setupRFModule();
 testRadio.replies.push_back({0,0});testRadio.replies.push_back({0,1});
 CHECK(g_bp->press());CHECK(g_bp->release());CHECK(g_bp->getLastKnownDeviceStatus()==ON);
 CHECK(!g_bp->pauseShutter());
 freshDevice();testRadio.replies.push_back({0,1});CHECK(g_bp->toggle());CHECK(testRadio.writes==1);CHECK(testRadio.lastPayload[0]==0x35);
 freshDevice();d->setMode(DIMMER);g_bp->setupRFModule();
 testRadio.replies.push_back({1,0});testRadio.replies.push_back({1,0});CHECK(g_bp->dimmerMem());CHECK(d->getStatus()==ON);
 freshDevice();for(unsigned i=0;i<300;++i)d->pollingFailed();CHECK(d->getFailedPollings()==255);d->pollingSuccess();CHECK(d->getFailedPollings()==0);

 // Configuration strings are bounded and default port is deterministic.
 alignas(MqttConfig) unsigned char storage[sizeof(MqttConfig)];memset(storage,0xa5,sizeof(storage));
 MqttConfig* conf=new(storage)MqttConfig();CHECK(conf->getPort()==1883);
 CHECK(conf->setHost("baseline"));CHECK(!conf->setHost(std::string(80,'x').c_str()));CHECK(std::string(conf->getHost())=="baseline");
 CHECK(conf->setUsername(nullptr));CHECK(conf->setPassword(nullptr));CHECK(!conf->setPort(0));
 CHECK(conf->setPassword(std::string(31,'x').c_str()));CHECK(!conf->setPassword(std::string(32,'x').c_str()));CHECK(strlen(conf->getPassword())==31);
 CHECK(!conf->setPassword("bad|field"));conf->~MqttConfig();
 uint32_t number=0;for(auto text:{"", "-1", "65536", "99999", "12x", " 12", "+12"})CHECK(!Yokis::unsignedNumber(text,65535,number));
 CHECK(Yokis::unsignedNumber("65535",65535,number)&&number==65535);

 testFs.files.clear();MqttConfig good("broker",1884,"","");CHECK(good.saveToLittleFS());
 MqttConfig roundtrip=MqttConfig::loadFromLittleFS();CHECK(std::string(roundtrip.getHost())=="broker");CHECK(roundtrip.getPort()==1884);CHECK(std::string(roundtrip.getPassword()).empty());
 std::string old=*testFs.files["/mqtt.conf"];testFs.failRename=true;good.setHost("new");CHECK(!good.saveToLittleFS());CHECK(*testFs.files["/mqtt.conf"]==old);testFs.failRename=false;
 testFs.failWrites=true;CHECK(!good.saveToLittleFS());CHECK(*testFs.files["/mqtt.conf"]==old);testFs.failWrites=false;
 CHECK(!testFs.files.count("/mqtt.conf.tmp"));

 // Parse/roundtrip legacy config and reject bad/incomplete records atomically.
 const char* line="volet|0102|2f|00|00|49ff06|1106|0003";
 Device parsed("");CHECK(Device::parseConfigLine(line,parsed));CHECK(parsed.getMode()==SHUTTER);CHECK(parsed.getChannel()==47);
 char formatted[128];CHECK(parsed.formatConfigLine(formatted,sizeof(formatted)));CHECK(std::string(formatted)==line);
 for(auto invalid:{"", "x|123", "x|abcd|ff|00|00|490020|0000|0003", "x|abcd|02|00|00|490020|0000|0009", "x|abcd|02|00|00|490020|0000|0003|extra"}) CHECK(!Device::parseConfigLine(invalid,parsed));
 CHECK(!Device::parseConfigLine(std::string(200,'x').c_str(),parsed));
 CHECK(Device::storeRawConfig(line));CHECK(Device::storeRawConfig(line));
 Device* loaded[64]={};CHECK(Device::loadFromLittleFS(loaded,64)==1);delete loaded[0];loaded[0]=nullptr;
 old=*testFs.files["/yokis.conf"];testFs.failRename=true;CHECK(!Device::deleteFromConfig("volet"));CHECK(*testFs.files["/yokis.conf"]==old);testFs.failRename=false;
 testFs.files["/yokis.conf"]=std::make_shared<std::string>(std::string(line)+"\nmalformed\n");CHECK(Device::loadFromLittleFS(loaded,64)==-1);CHECK(loaded[0]==nullptr);
 Device* previous=g_devices[0];CHECK(!reloadConfig(nullptr));CHECK(g_devices[0]==previous);
 testFs.files["/yokis.conf"]=std::make_shared<std::string>(std::string(line)+"\n");

 // Subscription capacity counts topics, does not fill up on re-discovery.
 g_mqtt->connected_=true;g_mqtt->clearSubscriptions();bool all=true;
 for(int i=0;i<80;++i){all=g_mqtt->subscribe("volet/cmnd/POWER")&&all;}CHECK(all);
 g_mqtt->clearSubscriptions();g_mqtt->clearSubscriptions();
 all=true;for(int i=0;i<64;++i){std::string name="light"+std::to_string(i);Device light(name.c_str(),address,1);light.setMode(DIMMER);all=g_mqtt->subscribeDevice(&light)&&all;}CHECK(all);
 g_mqtt->clearSubscriptions();g_mqtt->failSubscribe=true;CHECK(!g_mqtt->subscribeDevice(d));g_mqtt->failSubscribe=false;
 g_mqtt->setDiscoveryDone(false);g_mqtt->failSubscribe=true;loop();CHECK(!g_mqtt->isDiscoveryDone());
 g_mqtt->failSubscribe=false;testClockUs+=200000;loop();CHECK(g_mqtt->isDiscoveryDone());

 // A partial HTTP MQTT change preserves every omitted field; invalid requests
 // queue nothing. Production handler and deferred apply are both executed.
 CHECK(g_mqtt->setConnectionInfo("keep-host",1883,"keep-user","keep-pass",false));
 AsyncWebServerRequest r;r.params["mqtt_port"]=AsyncWebParameter("1884");webserver.handlers["/save_config"](&r);CHECK(r.status==202);
 testClockUs+=200000;webserver.processPending();CHECK(std::string(g_mqtt->getHost())=="keep-host");CHECK(g_mqtt->getPort()==1884);
 CHECK(std::string(g_mqtt->getUsername())=="keep-user");CHECK(std::string(g_mqtt->getPassword())=="keep-pass");
 for(auto port:{"abc","0","99999","-1"}){AsyncWebServerRequest bad;bad.params["mqtt_port"]=AsyncWebParameter(port);webserver.handlers["/save_config"](&bad);CHECK(bad.status==400);}
 CHECK(g_mqtt->getPort()==1884);

 // CLI: malformed arguments have no effects, LF/CRLF and overlong lines do
 // not overflow or execute a tail as a command. Keep the exact flag toggle.
 CHECK(!pressForCallback("pressFor volet"));CHECK(!mqttConfig("mqttConfig broker 99999"));CHECK(!restoreConfig("dRestore"));
 CHECK(!dimmerMinCallback("dimmin missing"));CHECK(!storeConfigCallback("save"));
 Yokis::Arguments args("wifiConfig \"ssid with spaces\" \"password with spaces\"");CHECK(args.valid&&args.count==3&&std::string(args.at(1))=="ssid with spaces");
 Yokis::Arguments unclosed("wifiConfig \"broken");CHECK(!unclosed.valid);
 byte flags=g_ConfigFlags;Serial.feed("poll\n");drainSerial();CHECK(g_ConfigFlags==(flags^FLAG_POLLING));
 Serial.feed("poll\r\n");drainSerial();CHECK(g_ConfigFlags==flags);
 Serial.feed(std::string(32,'x')+"\r");drainSerial();CHECK(g_ConfigFlags==flags);
 Serial.feed(std::string(300,'x')+"poll\n");drainSerial();CHECK(g_ConfigFlags==flags);
 // Full table used by dConfig must be bounded, with no sentinel past slot 63.
 for(int i=1;i<64;++i){g_devices[i]=d;}CHECK(displayDevices(nullptr));for(int i=1;i<64;++i){g_devices[i]=nullptr;}

 testPostStopPolling(d, wifi);
 testNetwork(d, wifi);

 // Save/reload replaces borrowed pointers and clears subscriptions safely.
 CHECK(reloadConfig(nullptr));CHECK(g_bp->getDevice()==nullptr);CHECK(!g_mqtt->isDiscoveryDone());
 CHECK(mqttConfigDelete(nullptr));CHECK(!testFs.files.count("/mqtt.conf"));
 testYield={};
 delete g_bp;delete g_scanner;delete g_pairingRF;delete g_copy;delete g_currentDevice;delete g_serial;delete g_mqtt;
 for(unsigned i=0;i<64;++i){delete g_deviceStatusPollers[i];delete g_devices[i];}
 std::cout<<checks<<" checks, "<<failures<<" failures\n";
 return failures?1:0;
}

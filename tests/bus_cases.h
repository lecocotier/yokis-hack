#pragma once
#include "RF/bus.h"

void testBusMode() {
    const auto savedFiles=testFs.files; const auto savedYield=testYield;
    Device* saved[64]; Ticker* timers[64];
    std::copy(g_devices,g_devices+64,saved);
    std::copy(g_deviceStatusPollers,g_deviceStatusPollers+64,timers);
    std::fill(g_devices,g_devices+64,nullptr);
    std::fill(g_deviceStatusPollers,g_deviceStatusPollers+64,nullptr);
    testFs.files.clear();
    const uint8_t otherAddress[]={3,4,3,4,4};
    auto* entry=new Device("bus-entry",address,47); entry->setMode(SHUTTER);
    auto* other=new Device("bus-other",otherAddress,48); other->setMode(SHUTTER);
    auto* group=new Device("tous",address,47); group->setMode(SHUTTER_BUS);
    auto* light=new Device("not-a-member",otherAddress,49);light->setMode(DIMMER);light->setStatus(ON);
    g_devices[0]=entry;g_devices[1]=other;g_devices[2]=group;g_devices[3]=light;
    CHECK(group->getMode()==SHUTTER_BUS);
    CHECK(Yokis::busEntry(*group,g_devices,64)==entry);
    entry->setStatus(SHUTTER_OPENED);other->setStatus(SHUTTER_OPENED);
    testYield=[](){
        if(testRadio.irqStatus&0x20) IrqManager::processIRQ();
        if(!testRadio.fifo.empty())g_bp->interruptRxReady();
    };
    testRadio=RadioFixture();testRadio.autoTxSuccess=true;
    g_bp->setDevice(group);CHECK(g_bp->off());
    CHECK(testRadio.writes==1 && testRadio.txCycles==1);
    CHECK(!g_bp->hasResponse()); // TX_DS is transmission, not a member reply.
    CHECK(testRadio.lastPayload==std::vector<uint8_t>({0xfa,0x16,0,0x20,0,0,127,0,0}));
    CHECK(entry->getStatus()==SHUTTER_CLOSING && other->getStatus()==SHUTTER_CLOSING);
    CHECK(light->getStatus()==ON);
    CHECK(std::string(other->shutterFeedback().sourceName())=="bus_command_estimate");
    CHECK(other->shutterFeedback().estimated() && !other->shutterFeedback().rawValid());
    CHECK(!other->shutterFeedback().commandResponse());

    // Individual STOP gets its own reply/context. Polling the entry cannot
    // broadcast its observation to the individually stopped member.
    g_bp->setDevice(other);testRadio.replies.push_back({0,0});CHECK(g_bp->pauseShutter());
    CHECK(testRadio.lastPayload[1]==6 && testRadio.lastPayload[3]==0);
    CHECK(other->getStatus()==SHUTTER_STOPPED && entry->getStatus()==SHUTTER_CLOSING);
    CHECK(other->shutterFeedback().verification().pending());
    pollReply(other,0,0);pollReply(entry,0,1);
    CHECK(other->getStatus()==SHUTTER_STOPPED);
    CHECK(!Yokis::refreshBusState(*group,g_devices,64)); // Poll integration already refreshed it.
    CHECK(group->getStatus()==UNDEFINED);
    const unsigned beforePoll=testRadio.writes;
    pollForStatus(group);
    CHECK(testRadio.writes==beforePoll && other->getStatus()==SHUTTER_STOPPED);

    // A fresh group order replaces each member's previous command separately.
    testRadio=RadioFixture();testRadio.autoTxSuccess=true;g_bp->setDevice(group);
    CHECK(g_bp->on());CHECK(other->getStatus()==SHUTTER_OPENING);
    CHECK(!other->shutterFeedback().verification().pending());
    CHECK(group->getStatus()==SHUTTER_OPENING);

    testRadio=RadioFixture();testRadio.autoTxSuccess=true;
    g_mqtt->connected_=true;g_mqtt->input.stopped=false;
    g_mqtt->publications.clear();
    g_mqtt->setDiscoveryDone(true);
    deliver("tous/cmnd/POWER","PAUSE");
    CHECK(g_mqtt->publications.size()<=4); // Do not synchronously publish all 64 members.
    for(unsigned i=0;i<2;++i){testClockUs+=200000;g_mqtt->serviceRefresh(g_devices,64);}
    CHECK(published("bus-other/tele/DETAIL").find("bus_command_estimate")!=std::string::npos);
    pollReply(other,0,0);
    CHECK(other->getStatus()==SHUTTER_STOPPED);
    testRadio=RadioFixture();testRadio.autoTxSuccess=true;
    deliver("tous/cmnd/POWER","OFF");
    testRadio.replies.push_back({0,0});deliver("bus-other/cmnd/POWER","PAUSE");
    CHECK(group->getStatus()==UNDEFINED);
    CHECK(published("tous/tele/STATE")=="{\"POWER\":\"None\"}");
    CHECK(g_mqtt->publishDevice(group));
    const auto discovery=published("homeassistant/cover/tous/config");
    CHECK(discovery.find("\"payload_stop\":\"PAUSE\"")!=std::string::npos);
    std::ofstream("tests/build/bus-discovery.json")<<discovery;

    // A stale TX_DS before a new command cannot confirm it; no bus retries.
    testRadio=RadioFixture();testRadio.irqStatus=0x20;g_bp->setDevice(group);
    CHECK(!g_bp->off());CHECK(testRadio.writes==1 && testRadio.txCycles==1);
    CHECK(entry->getStatus()==UNDEFINED && other->getStatus()==UNDEFINED);
    // An individual STOP still needs an application response, even with TX_DS.
    testRadio=RadioFixture();testRadio.autoTxSuccess=true;g_bp->setDevice(other);
    CHECK(!g_bp->pauseShutter());CHECK(!g_bp->hasResponse());

    // Resolve the entry at execution, never retain a borrowed route pointer.
    entry->setMode(DIMMER);testRadio=RadioFixture();g_bp->setDevice(group);
    CHECK(!g_bp->on());CHECK(testRadio.writes==0);
    entry->setMode(SHUTTER);
    char line[128];CHECK(group->formatConfigLine(line,sizeof(line)));
    Device parsed("");CHECK(Device::parseConfigLine(line,parsed));CHECK(parsed.getMode()==SHUTTER_BUS);
    CHECK(!busConfig("busConfig bus-entry bus-other")); // cannot overwrite an individual
    CHECK(!busConfig("busConfig nouveau missing"));
    CHECK(entry->saveToLittleFS());CHECK(other->saveToLittleFS());
    CHECK(light->saveToLittleFS());
    const unsigned beforeConfig=testRadio.writes;
    CHECK(busConfig("busConfig nouveau bus-entry"));
    CHECK(testRadio.writes==beforeConfig);
    Device* restored=Device::getFromList(g_devices,64,"nouveau");
    CHECK(restored && restored->getMode()==SHUTTER_BUS);
    if(restored) CHECK(Yokis::busEntry(*restored,g_devices,64)!=nullptr);
    for(unsigned i=0;i<64;++i) if(g_devices[i]&&g_devices[i]->getMode()==SHUTTER_BUS)
        CHECK(g_deviceStatusPollers[i]==nullptr);

    g_bp->setDevice(nullptr);
    for(unsigned i=0;i<64;++i){delete g_devices[i];delete g_deviceStatusPollers[i];}
    std::copy(saved,saved+64,g_devices);std::copy(timers,timers+64,g_deviceStatusPollers);
    testFs.files=savedFiles;testYield=savedYield;testRadio=RadioFixture();
    g_mqtt->setDiscoveryDone(false);
}

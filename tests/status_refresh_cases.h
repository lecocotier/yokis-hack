#pragma once
#include "statusRefresh.h"
#include "statusLed.h"

void testStatusRefresh() {
    freshDevice();
    const byte flags=g_ConfigFlags;g_ConfigFlags=0;
    Device* other=new Device("refresh-other",address,48);other->setMode(SHUTTER);g_devices[1]=other;
    Device* group=new Device("refresh-bus",address,47);group->setMode(SHUTTER_BUS);g_devices[2]=group;
    CHECK(!statusAllCallback("statusAll extra"));
    CHECK(statusAllCallback("statusAll"));CHECK(testRadio.writes==0);
    testRadio.replies.push_back({0,1});testRadio.replies.push_back({1,1});
    loop();CHECK(testRadio.writes==1);
    loop();CHECK(testRadio.writes==2);
    loop();CHECK(testRadio.writes==2); // no bus poll or automatic repeat
    CHECK(g_devices[0]->getStatus()==SHUTTER_CLOSING);
    CHECK(other->getStatus()==SHUTTER_OPENING);
    CHECK(statusAllCallback("statusAll"));
    g_mqtt->connected_=true;g_mqtt->input.stopped=false;
    g_mqtt->setCallback(mqttCallback);
    testRadio.replies.push_back({0,0});
    g_mqtt->input.packets.emplace_back("volet/cmnd/POWER","PAUSE");
    const unsigned before=testRadio.writes;
    loop();CHECK(testRadio.writes==before+1);
    CHECK(testRadio.lastPayload[0]==0x1a); // queued STOP wins over manual refresh
    Yokis::cancelStatusRefresh();
    g_devices[1]=nullptr;g_devices[2]=nullptr;delete other;delete group;
    g_ConfigFlags=flags;freshDevice();

    CHECK(Yokis::statusLedOn(0,false,false,true));
    CHECK(!Yokis::statusLedOn(150,false,false,true));
    CHECK(Yokis::statusLedOn(250,false,false,true));
    CHECK(!Yokis::statusLedOn(999,false,false,true));
    CHECK(Yokis::statusLedOn(500,true,false,true));
    CHECK(!Yokis::statusLedOn(1500,true,false,true));
    CHECK(!Yokis::statusLedOn(0,true,true,true));
    CHECK(!Yokis::statusLedOn(0,true,false,false));
}

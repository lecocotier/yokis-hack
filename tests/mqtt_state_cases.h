#pragma once

void testMqttUnknownReplay() {
    const uint64_t savedClock=testClockUs;
    WiFiClient input;
    MqttHass transport(input,"fixture-broker",1883,"","");
    transport.connected_=true;input.stopped=false;
    Device light("fixtureDimmer",address,47);light.setMode(DIMMER);
    const auto state=[&]() {
        std::string value;
        for(const auto& publication:transport.publications)
            if(publication.first=="fixtureDimmer/tele/STATE")value=publication.second;
        return value;
    };
    for(DimmerBrightness brightness:{BRIGHTNESS_OFF,BRIGHTNESS_MAX}) {
        light.setBrightness(brightness);light.setStatus(UNDEFINED);
        const auto stamp=light.getLastUpdateMillis();
        transport.publications.clear();
        CHECK(transport.notifyBrightness(&light,true));
        CHECK(state()=="{\"POWER\":\"UNDEFINED\"}");
        CHECK(light.getStatus()==UNDEFINED&&light.getLastUpdateMillis()==stamp);
        transport.publications.clear();transport.setDiscoveryDone(false);
        Device* devices[64]={&light};
        for(unsigned i=0;i<4;++i){testClockUs+=200000;transport.serviceRefresh(devices,64);}
        CHECK(state()=="{\"POWER\":\"UNDEFINED\"}");
        CHECK(light.getStatus()==UNDEFINED&&light.getLastUpdateMillis()==stamp);
    }
    light.setStatus(ON);light.setBrightness(BRIGHTNESS_MAX);
    CHECK(transport.notifyBrightness(&light,true));CHECK(state()=="{\"POWER\":\"ON\"}");
    light.setStatus(OFF);
    CHECK(transport.notifyBrightness(&light,true));CHECK(state()=="{\"POWER\":\"OFF\"}");
    testClockUs=savedClock;
}

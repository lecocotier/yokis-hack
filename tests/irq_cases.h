void testIrqDispatch() {
    const auto savedYield=testYield;
    testRadio=RadioFixture();IrqManager::irqType=E2BP;testRadio.irqStatus=0x70;
    IrqManager::processIRQ();
    CHECK(testRadio.irqStatus==0);
    CHECK((testRadio.irqEvents==std::vector<std::string>{"status","reg:0:15","ce:1","reg:7:112"}));
    CHECK(testRadio.config==15);
    testRadio.irqEvents.clear();testRadio.irqStatus=0x20;testRadio.irqFailsOnce=true;testIrqPin()=LOW;
    IrqManager::servicePending();CHECK(testIrqPin()==LOW);CHECK(testRadio.irqStatus==0x20);
    IrqManager::servicePending();CHECK(testIrqPin()==HIGH);CHECK(testRadio.irqStatus==0);CHECK(testRadio.irqEvents.size()==5);
    Pairing* saved=g_pairingRF;g_pairingRF=nullptr;IrqManager::irqType=PAIRING;
    IrqManager::processIRQ();g_pairingRF=saved; // Startup/uninitialized target.
    testRadio=RadioFixture();unsigned packets=0;
    testYield=[&](){
        if(packets>=2)return;
        testRadio.fifo=packets?std::vector<uint8_t>{1,2,47,6,7}:std::vector<uint8_t>{9,8,7};
        ++packets;IrqManager::processIRQ();
    };
    CHECK(g_pairingRF->hackPairing());CHECK(packets==2);
    uint8_t version[3],serial[2],hardware[5];
    g_pairingRF->getVersionFromRecvData(version);g_pairingRF->getSerialFromRecvData(serial);
    g_pairingRF->getAddressFromRecvData(hardware);
    CHECK(version[0]==9&&version[2]==7);CHECK(serial[0]==6&&serial[1]==7);
    CHECK(hardware[0]==1&&hardware[1]==2&&hardware[4]==2);CHECK(g_pairingRF->getChannelFromRecvData()==47);
    CHECK((testRadio.irqEvents==std::vector<std::string>{"status","read:3","reg:17:5","status","read:5","ce:0"}));
    testRadio=RadioFixture();testRadio.irqReadFails=true;
    testYield=[](){testRadio.fifo={1,2,3};IrqManager::processIRQ();};
    CHECK(!g_pairingRF->hackPairing()); // Failed bus data must not become a valid pairing.
    testYield=savedYield;IrqManager::irqType=E2BP;testRadio=RadioFixture();
}

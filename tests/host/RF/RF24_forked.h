#pragma once
#include <Arduino.h>
#include <nRF24L01.h>
struct RadioFixture {
 uint8_t channel=0;std::vector<uint8_t> address;
 std::vector<std::pair<uint8_t,std::vector<uint8_t>>> destinations;
 std::vector<std::vector<uint8_t>> payloads;
 bool connected=true;uint8_t config=0,payloadSize=9;unsigned writes=0,txCycles=0;
 std::deque<std::vector<uint8_t>> replies;
 std::vector<uint8_t> fifo,lastPayload;bool pending=false,active=false;
};
extern RadioFixture testRadio;
class RF24_forked {
public:
 RF24_forked(uint16_t,uint16_t){}
 bool begin(){return testRadio.connected;}
 uint8_t write_register(uint8_t reg,uint8_t v){if(reg==NRF_CONFIG)testRadio.config=v;if(reg==RF_CH)testRadio.channel=v;return 0;}
 uint8_t write_register(uint8_t reg,const uint8_t* a,uint8_t n){if(reg==TX_ADDR)testRadio.address.assign(a,a+n);return 0;}
 uint8_t read_register(uint8_t){return testRadio.fifo.empty()?1:0;}
 uint8_t get_status(){return testRadio.fifo.empty()?0:_BV(RX_DR);}
 void ce(bool high){if(high&&testRadio.config==14&&testRadio.active){++testRadio.txCycles;if(testRadio.pending){testRadio.pending=false;testRadio.fifo=testRadio.replies.front();testRadio.replies.pop_front();}}}
 void setPayloadSize(uint8_t n){testRadio.payloadSize=n;}
 uint8_t getPayloadSize(){return testRadio.payloadSize;}
 uint8_t write_payload(const void*b,uint8_t n,uint8_t){++testRadio.writes;testRadio.destinations.emplace_back(testRadio.channel,testRadio.address);testRadio.lastPayload.assign((const uint8_t*)b,(const uint8_t*)b+n);testRadio.payloads.push_back(testRadio.lastPayload);testRadio.pending=!testRadio.replies.empty();testRadio.active=true;return 0;}
 void read(void*b,uint8_t n){for(uint8_t i=0;i<n;++i)((uint8_t*)b)[i]=i<testRadio.fifo.size()?testRadio.fifo[i]:0;testRadio.fifo.clear();}
 bool available(){return !testRadio.fifo.empty();}
 void flush_rx(){testRadio.fifo.clear();}
 void flush_tx(){testRadio.active=false;}
 void spiTrans(uint8_t){}
 void printDetails(){}
 void powerDown(){testRadio.active=false;}
 void whatHappened(bool&tx,bool&fail,bool&rx){tx=fail=false;rx=available();}
 void maskIRQ(bool,bool,bool){}
 void setCRCLength(rf24_crclength_e){}
 void setPALevel(rf24_pa_dbm_e){}
 void setChannel(uint8_t){}
 void setAutoAck(bool){}
 void setAddressWidth(uint8_t){}
 void openReadingPipe(uint8_t,const uint8_t*){}
 void startListening(){}
 void stopListening(){}
 void disableCRC(){}
 void setDataRate(rf24_datarate_e){}
 void openWritingPipe(const uint8_t*){}
 bool write(const void*,uint8_t){return true;}
};

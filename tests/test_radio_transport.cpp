#include <Arduino.h>
#include <SPI.h>
#include <RF/RF24_forked.h>
#include <RF/irqTransport.h>
#include <RF/radioCritical.h>
#include <iostream>

FakeLog Serial; FakeESP ESP; TestSPI SPI;
uint64_t testClockUs=0; std::function<void()> testYield;
static uint32_t irqState=0;
static unsigned attempts=0, unsafe=0, checks=0;
static bool failNextIrqByte=false;
static bool csnHigh=true,transactionActive=false,pendingIrq=false;
static unsigned deliveredIrqs=0;
static std::vector<uint8_t> sent;
static std::deque<uint8_t> received;
static std::vector<std::pair<uint16_t,bool>> pins;
#define CHECK(x) do {++checks;if(!(x)){std::cerr<<"radio failed "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
uint32_t radioTestDisable(){uint32_t old=irqState;irqState=15;return old;}
void radioTestRestore(uint32_t old){irqState=old;if(old==0&&pendingIrq){if(!csnHigh||transactionActive)++unsafe;pendingIrq=false;++deliveredIrqs;}}
uint8_t radioTestTransfer(uint8_t b){++attempts;pendingIrq=true;if(irqState!=15)++unsafe;sent.push_back(b);if(received.empty())return 0;auto r=received.front();received.pop_front();return r;}
bool radioTestIrqTransfer(uint8_t b,uint8_t& result){if(failNextIrqByte){failNextIrqByte=false;return false;}result=radioTestTransfer(b);return true;}
void radioTestCsn(uint16_t pin,bool high){csnHigh=high;pendingIrq=true;pins.emplace_back(pin,high);if(irqState!=15)++unsafe;}
void radioTestCe(uint16_t pin,bool high){pins.emplace_back(pin,high);}
void radioTestDelay(unsigned us){testClockUs+=us;}
void radioTestBoundary(bool start){if(irqState!=15||!csnHigh)++unsafe;transactionActive=start;pendingIrq=true;}
void radioTestDigitalWrite(int pin,int high){if(pin==15)radioTestCsn(uint16_t(pin),high!=0);else radioTestCe(uint16_t(pin),high!=0);}
int main(){
#ifndef RADIO_BASELINE
 {Yokis::RadioCritical outer;CHECK(irqState==15);{Yokis::RadioCritical inner;CHECK(irqState==15);}CHECK(irqState==15);outer.release();CHECK(irqState==0);}
 irqState=3;{Yokis::RadioCritical guard;CHECK(irqState==15);}CHECK(irqState==3);
#endif
 irqState=0;RF24_forked radio(4,15);uint8_t data[3]={1,2,3};
 radio.write_register(5,47);CHECK(irqState==0);CHECK(unsafe==0);CHECK(attempts==2);
 received={0x40,0x12,0x34,0x56};radio.read_register(0x10,data,3);CHECK(data[0]==0x12&&data[2]==0x56);CHECK(unsafe==0);CHECK(irqState==0);
 radio.write_register(0x10,data,3);radio.spiTrans(0xe1);radio.write_payload(data,3,0xa0);
 radio.setPayloadSize(3);received={0,1,2,3};radio.read_payload(data,3);
 received={0,3};CHECK(radio.getDynamicPayloadSize()==3);
 radio.writeAckPayload(0,data,3);radio.toggle_features();
 CHECK(unsafe==0);CHECK(irqState==0);CHECK(csnHigh&&!transactionActive);CHECK(deliveredIrqs>=9&&!pendingIrq);
#ifndef RADIO_BASELINE
 irqState=15;sent.clear();pins.clear();received={0x70,0};
 RadioIrqTransport irq(4,15);CHECK(irq.statusAndClear()==0x70);CHECK((sent==std::vector<uint8_t>{0x27,0x70}));
 sent.clear();irq.writeRegister(0,0x0f);irq.ce(true);irq.writeRegister(7,0x70);
 CHECK((sent==std::vector<uint8_t>{0x20,0x0f,0x27,0x70}));CHECK(unsafe==0);
 sent.clear();received={0x40,1,2,3};irq.readPayload(data,3);CHECK(data[0]==1&&data[2]==3);
 CHECK((sent==std::vector<uint8_t>{0x61,0xff,0xff,0xff}));
 irq.writeRegister(0x11,5);CHECK(sent[sent.size()-2]==0x31&&sent.back()==5);
 CHECK(pins.front().first==15&&!pins.front().second);CHECK(irqState==15);
 failNextIrqByte=true;CHECK(!irq.readPayload(data,3));CHECK(pins.back().second); // Release CSN even on fault.
 failNextIrqByte=true;CHECK(irq.statusAndClear()==0);CHECK(pins.back().second);
 received={0x70,0};CHECK(irq.statusAndClear()==0x70);CHECK(irqState==15);
#endif
 std::cout<<checks<<" radio transport checks passed\n";return 0;
}

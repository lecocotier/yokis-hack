#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cstdarg>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <memory>
#include <map>
using byte = uint8_t;
using boolean = bool;
using ulong = unsigned long;
#define ICACHE_RAM_ATTR
#define IRAM_ATTR
#define PROGMEM
#define HIGH 1
#define LOW 0
#define HEX 16
#define DEC 10
#define OUTPUT 1
#define INPUT_PULLUP 2
#define FALLING 2
#define F(s) s
class String {
public:
 std::string s;
 String() = default;
 String(const char* p):s(p?p:""){}
 String(const std::string& p):s(p){}
 String(unsigned long v, int base=10){char b[40];std::snprintf(b,sizeof(b),base==16?"%lx":"%lu",v);s=b;}
 String(long v, int base=10):String(static_cast<unsigned long>(v),base){}
 String(int v, int base=10):String(static_cast<unsigned long>(v),base){}
 String(unsigned int v, int base=10):String(static_cast<unsigned long>(v),base){}
 const char* c_str()const{return s.c_str();}
 unsigned int length()const{return s.size();}
 String& operator+=(char c){s+=c;return *this;}
 String& operator+=(const String& v){s+=v.s;return *this;}
 bool operator==(const char* v)const{return s==v;}
 friend String operator+(const String&a,const String&b){return a.s+b.s;}
};
class Print {
 int error_=0;
public:
 virtual ~Print()=default;
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t* b,size_t n){size_t k=0;for(;k<n;++k)if(write(b[k])!=1)break;return k;}
 size_t write(const char* s){return write(reinterpret_cast<const uint8_t*>(s),std::strlen(s));}
 size_t print(const char* s){return write(s?s:"(null)");}
 size_t print(const String& s){return print(s.c_str());}
 size_t print(char c){return write(static_cast<uint8_t>(c));}
 size_t print(unsigned long n,int base=10){char b[40];std::snprintf(b,sizeof(b),base==16?"%lx":"%lu",n);return print(b);}
 size_t print(long n,int base=10){if(base==16)return print(static_cast<unsigned long>(n),base);char b[40];std::snprintf(b,sizeof(b),"%ld",n);return print(b);}
 size_t print(unsigned int n,int b=10){return print((unsigned long)n,b);}
 size_t print(int n,int b=10){return print((long)n,b);}
 size_t println(){return print("\r\n");}
 template<class T>size_t println(T t){return print(t)+println();}
 template<class T>size_t println(T t,int b){return print(t,b)+println();}
 size_t printf(const char* fmt,...){char b[2048];va_list a;va_start(a,fmt);int n=vsnprintf(b,sizeof(b),fmt,a);va_end(a);if(n<0)return 0;return write((const uint8_t*)b,std::min<size_t>(n,sizeof(b)-1));}
 int getWriteError()const{return error_;}
 void setWriteError(int e=1){error_=e;}
 void clearWriteError(){error_=0;}
};
class FakeLog:public Print {
public:
 std::string out;std::deque<uint8_t> in;
 using Print::write;
 size_t write(uint8_t c)override{out+=char(c);return 1;}
 void begin(unsigned long){}
 int available(){return in.size();}
 int read(){if(in.empty())return -1;int c=in.front();in.pop_front();return c;}
 void feed(const std::string&s){for(unsigned char c:s)in.push_back(c);}
 void flush(){}
 void handle(){}
};
extern FakeLog Serial;
extern uint64_t testClockUs;
extern std::function<void()> testYield;
inline unsigned long millis(){return static_cast<uint32_t>(testClockUs/1000);}
inline unsigned long micros(){return static_cast<uint32_t>(testClockUs);}
inline void yield(){testClockUs+=10;if(testYield)testYield();}
inline void delay(unsigned long n){testClockUs+=uint64_t(n)*1000;if(testYield)testYield();}
inline void delayMicroseconds(unsigned int n){testClockUs+=n;if(testYield)testYield();}
inline long random(long lo,long hi){return hi>lo?lo+(hi-lo)/2:lo;}
inline long random(long hi){return random(0,hi);}
inline void randomSeed(unsigned long){}
inline void noInterrupts(){}
inline void interrupts(){}
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline int digitalRead(int){return HIGH;}
inline int digitalPinToInterrupt(int p){return p;}
inline void attachInterrupt(int,void(*)(),int){}
inline void detachInterrupt(int){}
struct FakeESP{void restart(){}unsigned getFlashChipId(){return 1;}};
extern FakeESP ESP;

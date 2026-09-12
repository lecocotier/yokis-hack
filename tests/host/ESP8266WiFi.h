#pragma once
#include <Arduino.h>
#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP 2
#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_NONE_SLEEP 0
#define ETS_UART_INTR_DISABLE() ((void)0)
#define ETS_UART_INTR_ENABLE() ((void)0)
class WiFiClient {
public:
 std::deque<std::pair<std::string,std::string>> packets;
 bool stopped=false;
 int available(){return packets.empty()?0:1;}
 void stop(){stopped=true;}
};
class FakeWiFi {
public:
 String ssid,psk_,savedSsid,savedPsk;
 bool persistent_=false,autoReconnect_=false,autoConnect_=false;
 bool apReachable=true;
 int mode_=WIFI_STA,status_=WL_CONNECTED,beginCalls=0,reconnectCalls=0,eraseCalls=0,flashWrites=0;
 std::vector<std::string> events;
 String SSID(){return ssid;}String psk(){return psk_;}
 void printDiag(Print&){}int getMode(){return mode_;}
 String softAPIP(){return "192.0.2.1";}String localIP(){return "192.0.2.2";}
 void persistent(bool v){persistent_=v;events.push_back(v?"persistent:on":"persistent:off");}
 bool mode(int v){mode_=v;events.push_back("mode:"+std::to_string(v));return true;}
 void setSleepMode(int){}bool setAutoReconnect(bool v){autoReconnect_=v;return true;}
 bool setAutoConnect(bool v){autoConnect_=v;return true;}
 bool disconnect(bool off=false,bool erase=true){
  status_=WL_DISCONNECTED;if(off)mode_=WIFI_OFF;
  if(erase){++eraseCalls;ssid="";psk_="";if(persistent_){savedSsid="";savedPsk="";++flashWrites;}}
  return true;
 }
 int begin(const char*s,const char*p,int=0,const uint8_t* =nullptr,bool=true){
  ++beginCalls;ssid=s;psk_=p;mode_=WIFI_STA;
  if(persistent_){savedSsid=ssid;savedPsk=psk_;++flashWrites;}
  status_=apReachable?WL_CONNECTED:WL_DISCONNECTED;events.push_back("begin");return status_;
 }
 int begin(){++beginCalls;mode_=WIFI_STA;status_=apReachable&&ssid.length()?WL_CONNECTED:WL_DISCONNECTED;return status_;}
 bool reconnect(){++reconnectCalls;status_=apReachable&&ssid.length()?WL_CONNECTED:WL_DISCONNECTED;return status_==WL_CONNECTED;}
 int status(){return status_;}
 bool softAP(const String&,const char*){mode_=WIFI_AP;return true;}
 int waitForConnectResult(){return status_;}
};
extern FakeWiFi WiFi;
inline bool wifi_station_disconnect(){WiFi.status_=WL_DISCONNECTED;return true;}

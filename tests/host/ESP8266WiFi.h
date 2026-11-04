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
 struct SocketState{bool live=true;std::deque<uint8_t> input;std::string output;int writable=4096;};
 std::shared_ptr<SocketState> socket;
 std::deque<std::pair<std::string,std::string>> packets;
 bool stopped=false;
 static WiFiClient testClient(){WiFiClient c;c.socket=std::make_shared<SocketState>();return c;}
 void feed(const std::string& s){for(unsigned char c:s)socket->input.push_back(c);}
 std::string output()const{return socket?socket->output:"";}
 bool connected()const{return socket&&socket->live;}
 explicit operator bool()const{return connected();}
 int available(){return socket?int(socket->input.size()):(packets.empty()?0:1);}
 int read(){if(!socket||socket->input.empty())return -1;int c=socket->input.front();socket->input.pop_front();return c;}
 size_t write(uint8_t c){if(!connected()||!availableForWrite())return 0;socket->output+=char(c);return 1;}
 size_t write(const uint8_t* p,size_t n){size_t i=0;for(;i<n&&write(p[i]);++i){}return i;}
 size_t write(const char* p){return write(reinterpret_cast<const uint8_t*>(p),strlen(p));}
 int availableForWrite()const{return connected()?socket->writable:0;}
 void setAvailableForWrite(int n){if(socket)socket->writable=n;}
 void setNoDelay(bool){}
 void setTimeout(unsigned long){}
 void stop(){stopped=true;if(socket)socket->live=false;}
};
class WiFiServer {
public:
 struct State{bool listening=false;unsigned beginCalls=0;std::deque<WiFiClient> pending;};
 static std::shared_ptr<State> testState(uint16_t port){static std::map<uint16_t,std::shared_ptr<State>> states;auto& s=states[port];if(!s)s=std::make_shared<State>();return s;}
 explicit WiFiServer(uint16_t port):state_(testState(port)){}
 void begin(){state_->listening=true;++state_->beginCalls;}
 void close(){state_->listening=false;}
 void setNoDelay(bool){}
 bool hasClient(){return !state_->pending.empty();}
 WiFiClient accept(){if(!hasClient())return WiFiClient();auto c=state_->pending.front();state_->pending.pop_front();return c;}
private:std::shared_ptr<State> state_;
};
class FakeWiFi {
public:
 String ssid,psk_,savedSsid,savedPsk,apPassword;
 bool persistent_=false,autoReconnect_=false,autoConnect_=false;
 bool apReachable=true;
 int mode_=WIFI_STA,status_=WL_CONNECTED,beginCalls=0,reconnectCalls=0,eraseCalls=0,flashWrites=0;
 std::vector<std::string> events;
 String SSID(){return ssid;}String psk(){return psk_;}
 int RSSI(){return -65;}
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
 bool softAP(const String&,const char* password){mode_=WIFI_AP;apPassword=password;return true;}
 int waitForConnectResult(){return status_;}
};
extern FakeWiFi WiFi;
inline bool wifi_station_disconnect(){WiFi.status_=WL_DISCONNECTED;return true;}

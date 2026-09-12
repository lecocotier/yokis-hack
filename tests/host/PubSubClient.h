#pragma once
#include <ESP8266WiFi.h>
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024
#endif
class PubSubClient {
public:
 bool connected_=true,failSubscribe=false,failPublish=false,failConnect=false;
 struct Connection {std::string id,willTopic,willMessage;uint8_t qos;bool retained;};
 std::vector<Connection> connections;
 std::vector<bool> retained;
 unsigned disconnectCount=0;
 uint16_t socketTimeout=15,keepAlive=15;
 std::vector<std::string> events;
 std::vector<std::string> subscriptions,unsubscriptions;
 std::vector<std::pair<std::string,std::string>> publications;
 WiFiClient& input;
 void(*callback_)(char*,uint8_t*,unsigned int)=nullptr;
 explicit PubSubClient(WiFiClient& client):input(client){}
 bool connected(){return connected_ && !input.stopped;}
 bool subscribe(const char*s){if(failSubscribe)return false;subscriptions.emplace_back(s);return true;}
 bool unsubscribe(const char*s){unsubscriptions.emplace_back(s);return true;}
 bool publish(const char*t,const char*p,bool retain=false){if(failPublish||!connected_)return false;publications.emplace_back(t,p);retained.push_back(retain);events.push_back(std::string("publish:")+p);return true;}
 void setCallback(void(*cb)(char*,uint8_t*,unsigned int)){callback_=cb;}
 void disconnect(){++disconnectCount;events.push_back("disconnect");connected_=false;}
 void setServer(const char*,uint16_t){}
 bool connect(const char*id,const char*,const char*,const char*will=nullptr,uint8_t qos=0,bool retain=false,const char*msg=nullptr,bool=true){connections.push_back({id,will?will:"",msg?msg:"",qos,retain});input.stopped=false;return connected_=!failConnect;}
 PubSubClient& setSocketTimeout(uint16_t s){socketTimeout=s;return *this;}
 PubSubClient& setKeepAlive(uint16_t s){keepAlive=s;return *this;}
 bool loop(){
  // PubSubClient 2.8 handles one incoming MQTT packet per loop().
  if(connected_ && !input.packets.empty()) {
   auto msg=input.packets.front();input.packets.pop_front();
   std::vector<char> topic(msg.first.begin(),msg.first.end());topic.push_back(0);
   if(callback_)callback_(topic.data(),(uint8_t*)msg.second.data(),msg.second.size());
  }
  return connected_;
 }
 int state(){return 0;}
};

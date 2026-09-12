#pragma once
#include <ESP8266WiFi.h>
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024
#endif
class PubSubClient {
public:
 bool connected_=true,failSubscribe=false,failPublish=false;
 std::vector<std::string> subscriptions,unsubscriptions;
 std::vector<std::pair<std::string,std::string>> publications;
 WiFiClient& input;
 void(*callback_)(char*,uint8_t*,unsigned int)=nullptr;
 explicit PubSubClient(WiFiClient& client):input(client){}
 bool connected(){return connected_;}
 bool subscribe(const char*s){if(failSubscribe)return false;subscriptions.emplace_back(s);return true;}
 bool unsubscribe(const char*s){unsubscriptions.emplace_back(s);return true;}
 bool publish(const char*t,const char*p,bool=false){if(failPublish)return false;publications.emplace_back(t,p);return true;}
 void setCallback(void(*cb)(char*,uint8_t*,unsigned int)){callback_=cb;}
 void disconnect(){connected_=false;}
 void setServer(const char*,uint16_t){}
 bool connect(const char*,const char*,const char*){return connected_=true;}
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

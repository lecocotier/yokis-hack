#pragma once
#include <Arduino.h>
#define WIFI_AP 2
#define WL_DISCONNECTED 6
class WiFiClient {
public:
 std::deque<std::pair<std::string,std::string>> packets;
 int available(){return packets.empty()?0:1;}
};
class FakeWiFi{public:String ssid,psk_;String SSID(){return ssid;}String psk(){return psk_;}void printDiag(Print&){}int getMode(){return 1;}String softAPIP(){return "192.0.2.1";}String localIP(){return "192.0.2.2";}};
extern FakeWiFi WiFi;

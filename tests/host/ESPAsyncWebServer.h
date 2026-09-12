#pragma once
#include <Arduino.h>
#define HTTP_GET 1
class AsyncWebParameter {
 String v_;
public:
 AsyncWebParameter()=default;
 AsyncWebParameter(const char* s):v_(s){}
 const String& value()const{return v_;}
};
class AsyncWebServerRequest {
public:
 std::map<std::string,AsyncWebParameter> params;
 int status=0;std::string response;
 bool hasParam(const char*k){return params.count(k);}
 AsyncWebParameter* getParam(const char*k){return hasParam(k)?&params[k]:nullptr;}
 void send(int code,const char*,const char* body){status=code;response=body;}
 void send_P(int code,const char*,const char* body,String(*)(const String&)){status=code;response=body;}
 void redirect(const char* p){status=302;response=p;}
};
class AsyncWebServer {
public:
 std::map<std::string,std::function<void(AsyncWebServerRequest*)>> handlers;
 explicit AsyncWebServer(uint16_t){}
 template<class F>void on(const char* p,int,F f){handlers[p]=f;}
 void begin(){}
};

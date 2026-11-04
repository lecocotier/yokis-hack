#pragma once
#include <Arduino.h>
#define HTTP_GET 1
#define HTTP_POST 2
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
 std::map<std::string,AsyncWebParameter> bodyParams;
 std::string authUser,authPassword;
 int status=0;std::string response;
 bool hasParam(const char*k,bool post=false){return post?bodyParams.count(k):params.count(k);}
 AsyncWebParameter* getParam(const char*k,bool post=false){auto& p=post?bodyParams:params;return p.count(k)?&p[k]:nullptr;}
 bool authenticate(const char* u,const char* p){return authUser==u&&authPassword==p;}
 void requestAuthentication(){status=401;response="Authentication required";}
 void send(int code,const char*,const char* body){status=code;response=body;}
 void send_P(int code,const char* type,const char* body){send(code,type,body);}
 void send_P(int code,const char*,const char* body,String(*processor)(const String&)){
  status=code;response.clear();std::string s(body);
  for(size_t i=0;i<s.size();){if(s[i]!='%'){response+=s[i++];continue;}size_t end=s.find('%',i+1);if(end==std::string::npos){response+=s.substr(i);break;}if(end==i+1)response+='%';else response+=processor(String(s.substr(i+1,end-i-1))).s;i=end+1;}
 }
 void redirect(const char* p){status=302;response=p;}
};
class AsyncWebServer {
public:
 std::map<std::pair<std::string,int>,std::function<void(AsyncWebServerRequest*)>> handlers;
 bool started=false;
 explicit AsyncWebServer(uint16_t){}
 template<class F>void on(const char* p,int method,F f){handlers[{p,method}]=f;}
 void dispatch(const char* p,int method,AsyncWebServerRequest* r){auto it=handlers.find({p,method});if(it==handlers.end())r->send(405,"text/plain","Method not allowed");else it->second(r);}
 void begin(){started=true;}
};

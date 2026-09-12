#pragma once
#include <Arduino.h>
struct FakeFsState {std::map<std::string,std::shared_ptr<std::string>> files;bool failWrites=false,failRename=false,failOpen=false;};
extern FakeFsState testFs;
class File:public Print {
 std::shared_ptr<std::string> data_;size_t pos_=0;bool writable_=false;
public:
 File()=default;
 File(std::shared_ptr<std::string>d,bool w,bool append):data_(d),pos_(append?d->size():0),writable_(w){}
 explicit operator bool()const{return bool(data_);}
 using Print::write;
 size_t write(uint8_t c)override{if(!data_||!writable_||testFs.failWrites){setWriteError();return 0;}if(pos_==data_->size())data_->push_back(char(c));else (*data_)[pos_]=char(c);++pos_;return 1;}
 int available(){return data_&&pos_<data_->size()?data_->size()-pos_:0;}
 int read(){return available()?static_cast<uint8_t>((*data_)[pos_++]):-1;}
 size_t readBytesUntil(char delim,char*b,size_t n){size_t i=0;while(i<n&&available()){int c=read();if(c==delim)break;b[i++]=char(c);}return i;}
 String readStringUntil(char d){std::string s;while(available()){int c=read();if(c==d)break;s+=char(c);}return s;}
 size_t size()const{return data_?data_->size():0;}
 bool seek(size_t p){if(!data_||p>data_->size())return false;pos_=p;return true;}
 void flush(){}
 void close(){data_.reset();}
};
class FakeLittleFS {
public:
 bool begin(){return true;}
 bool exists(const char*p){return testFs.files.count(p);}
 File open(const char*p,const char*m){if(testFs.failOpen)return {};bool w=m[0]!='r';bool a=m[0]=='a';auto it=testFs.files.find(p);if(it==testFs.files.end()){if(!w)return {};it=testFs.files.emplace(p,std::make_shared<std::string>()).first;}if(w&&!a)it->second->clear();return File(it->second,w,a);}
 bool remove(const char*p){return testFs.files.erase(p)>0;}
 bool rename(const char*from,const char*to){if(testFs.failRename||!exists(from))return false;testFs.files[to]=testFs.files[from];testFs.files.erase(from);return true;}
};
extern FakeLittleFS LittleFS;

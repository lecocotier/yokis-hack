#pragma once
#include <Arduino.h>
inline int os_get_random(unsigned char* out,size_t size){static uint32_t n=1;for(size_t i=0;i<size;++i){n=n*1664525u+1013904223u;out[i]=uint8_t(n>>24);}return 0;}

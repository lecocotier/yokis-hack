#pragma once
#include "../host/Arduino.h"
#ifndef _BV
#define _BV(x) (1u << (x))
#endif
#define INPUT 0
#include "radioTestHardware.h"
#define digitalWrite radioTestDigitalWrite

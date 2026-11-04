#pragma once
#include <stdio.h>
#include <string.h>
#define PROGMEM
#define PSTR(x) (x)
#define printf_P printf
#define strlen_P strlen
#define pgm_read_byte(p) (*(const unsigned char*)(p))
#define pgm_read_ptr(p) (*(p))
#define pgm_read_word(p) (*(p))

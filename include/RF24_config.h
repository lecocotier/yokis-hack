/* Adapted from nRF24/RF24 2af44fcac5b0022780a18fddde593359194c634d.
 * Copyright (C) 2011 J. Coliz, 2015-2019 TMRh20 and RF24 contributors.
 * GPL-2.0-only; see doc/GPL-2.0.txt. Configuration for this project's AVR/ESP8266
 * hardware SPI targets. No second RF24 implementation is compiled.
 */
#ifndef YOKIS_RF24_CONFIG_H
#define YOKIS_RF24_CONFIG_H
#include <Arduino.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>
#if defined(ESP8266)
#include <pgmspace.h>
#define PRIPSTR "%s"
#else
#include <avr/pgmspace.h>
#define PRIPSTR "%S"
#endif
#define FAILURE_HANDLING
#define RF24_POWERUP_DELAY 5000
#define RF24_SPI_SPEED 10000000
#define rf24_max(a, b) ((a) > (b) ? (a) : (b))
#define rf24_min(a, b) ((a) < (b) ? (a) : (b))
#define IF_SERIAL_DEBUG(x)
#define _SPI SPI
#if defined(SPI_HAS_TRANSACTION)
#define RF24_SPI_TRANSACTIONS
#endif
#endif

/* RF24 public enum values retained from nRF24/RF24 commit 2af44fc.
 * Copyright (C) 2011 J. Coliz <maniacbug@ymail.com>, GPL-2.0-only.
 * These values are part of the existing radio configuration contract.
 */
#pragma once
typedef enum { RF24_PA_MIN = 0, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX, RF24_PA_ERROR } rf24_pa_dbm_e;
typedef enum { RF24_1MBPS = 0, RF24_2MBPS, RF24_250KBPS } rf24_datarate_e;
typedef enum { RF24_CRC_DISABLED = 0, RF24_CRC_8, RF24_CRC_16 } rf24_crclength_e;

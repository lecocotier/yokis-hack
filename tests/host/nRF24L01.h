#pragma once
#define RF_CH 5
#define RF_SETUP 6
#define RX_ADDR_P0 10
#define TX_ADDR 16
#define RX_PW_P0 17
#define EN_RXADDR 2
#define EN_AA 1
#define NRF_STATUS 7
#define SETUP_RETR 4
#define NRF_CONFIG 0
#define REUSE_TX_PL 227
#define W_TX_PAYLOAD 160
#define RX_DR 6
#define TX_DS 5
#define MAX_RT 4
#define FIFO_STATUS 23
#define RX_EMPTY 0
#define TX_EMPTY 4
#ifndef _BV
#define _BV(x) (1u<<(x))
#endif
enum rf24_crclength_e{RF24_CRC_16};
enum rf24_pa_dbm_e{RF24_PA_LOW,RF24_PA_HIGH};
enum rf24_datarate_e{RF24_250KBPS};

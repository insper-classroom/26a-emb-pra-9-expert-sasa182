#ifndef HC06_H_
#define HC06_H_

#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define HC06_UART_ID uart1
#define HC06_BAUD_RATE 115200
#define HC06_STATE_PIN 7
#define HC06_RX_PIN 4
#define HC06_TX_PIN 5
#define HC06_ENABLE_PIN 6

bool hc06_check_connection();
bool hc06_set_name(char name[]);
bool hc06_set_pin(char pin[]);
bool hc06_set_baud_115200();
bool hc06_set_at_mode(int on);
bool hc06_config(char name[], char pin[]);


#endif // HC06_H_

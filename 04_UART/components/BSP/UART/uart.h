#ifndef __UART_H__
#define __UART_H__

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "driver/uart_select.h"
#include "driver/gpio.h"

#define USART_UX UART_NUM_0
#define USART_TX_GPIO_PIN GPIO_NUM_43
#define USART_RX_GPIO_PIN GPIO_NUM_44

#define RX_BUF_SIZE 1024

void uart_init(uint32_t baudrate);

#endif
#ifndef __ESPTIM_H__
#define __ESPTIM_H__ 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led.h"
#include "esp_timer.h"

void esptim_cb(void* arg);
void esptim_init(uint64_t tps);

#endif
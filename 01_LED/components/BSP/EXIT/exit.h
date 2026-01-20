#ifndef __EXIT_H__
#define __EXIT_H__

#include "driver/gpio.h"
#include "esp_system.h"
#include "led.h"

#define BOOT_INT_GPIO_PIN GPIO_NUM_0 // boot
// #define BOOT gpio_get_level(BOOT_INT_GPIO_PIN)

void exit_init(void);

#endif
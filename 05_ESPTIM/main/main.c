#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "esptim.h"

void app_main(void)
{
	led_init();
	esptim_init(1000000);
}

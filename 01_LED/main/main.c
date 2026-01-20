#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "key.h"
#include "exit.h"

void app_main(void)
{
	led_init();
	key_init();
	uint8_t key;

	exit_init();

	while (1)
	{
		// LED_TOGGLE();
		// vTaskDelay(pdMS_TO_TICKS(500));

		// key = key_scan(0);
		// switch (key)
		// {
		// case BOOT_PRES:
		// 	LED_TOGGLE();
		// 	break;
		
		// default:
		// 	break;
		// }
		// vTaskDelay(pdMS_TO_TICKS(10));

		vTaskDelay(pdMS_TO_TICKS(10));
	}
	
}


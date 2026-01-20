#include "key.h"

void key_init(void)
{
	gpio_config_t gpio_init_struct = {0};
	gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
	gpio_init_struct.mode = GPIO_MODE_INPUT;
	gpio_init_struct.pull_down_en = GPIO_PULLDOWN_ENABLE;
	gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_init_struct.pin_bit_mask = (1ULL << BOOT_GPIO_PIN);

	gpio_config(&gpio_init_struct);

}

uint8_t key_scan(uint8_t mode)
{
	uint8_t key_val = 0;
	static uint8_t key_boot = 1;
	if(mode == 1)
	{
		key_boot = 1;
	}
	if(key_boot && BOOT == 0)
	{
		vTaskDelay(pdMS_TO_TICKS(10));
		key_boot = 0;
		if(BOOT == 0)
		{
			key_val = BOOT_PRES;
		}
	}
	else
	{
		key_boot = 1;
	}

	return key_val;
}
#include "led.h"

void led_init(void)
{
	gpio_config_t gpio_init_struct = {0};
	gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
	gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;
	gpio_init_struct.pull_down_en = GPIO_PULLDOWN_ENABLE;
	gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_init_struct.pin_bit_mask = (1ULL << LED_GPIO_PIN);

	gpio_config(&gpio_init_struct);

	LED(0);
}
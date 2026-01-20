#include "exit.h"
#include "esp_attr.h"


void IRAM_ATTR exit_gpio_isr_handler(void *arg)
{
	// 取出对应的引脚
	gpio_num_t gpio_num = (gpio_num_t)arg;

	// 判断是不是boot触发的中断
	if (gpio_num == BOOT_INT_GPIO_PIN)
	{
		LED_TOGGLE();
	}
}

void exit_init(void)
{
	gpio_config_t gpio_init_struct = {0};
	gpio_init_struct.intr_type = GPIO_INTR_NEGEDGE;
	gpio_init_struct.mode = GPIO_MODE_INPUT;
	gpio_init_struct.pull_down_en = GPIO_PULLDOWN_ENABLE;
	gpio_init_struct.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_init_struct.pin_bit_mask = (1ULL << BOOT_INT_GPIO_PIN);

	gpio_config(&gpio_init_struct);

	// 注册中断 优先级
	gpio_install_isr_service(0);

	gpio_isr_handler_add(BOOT_INT_GPIO_PIN, exit_gpio_isr_handler, (void *)BOOT_INT_GPIO_PIN);
}
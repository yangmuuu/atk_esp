#include "esptim.h"

void esptim_cb(void* arg)
{
	LED_TOGGLE();
}

void esptim_init(uint64_t tps)
{
	esp_timer_handle_t esp_timer_handle; // 定义定时器句柄

	esp_timer_create_args_t timer_config = {
		.callback = esptim_cb, // 定时器回调函数
		.arg = NULL, // 回调函数参数
	};

	esp_timer_create(&timer_config, &esp_timer_handle); // 创建定时器

	esp_timer_start_periodic(esp_timer_handle, tps); // 定时器开始工作 us
}
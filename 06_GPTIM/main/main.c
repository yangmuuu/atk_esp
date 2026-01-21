#include "gptim.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    led_init();

    // 使用栈或静态内存，避免 malloc 忘记 free 的问题
    static timg_config_t my_tim_cfg;
    my_tim_cfg.timing_us = 1000000; // 1秒
    my_tim_cfg.timer_count = 0;

    timg_new_init(&my_tim_cfg);

    while (1)
    {
        if(my_tim_cfg.timer_count != 0)
        {
            ESP_LOGI("TIMER", "Alarm triggered at: %llu us", my_tim_cfg.timer_count);
            my_tim_cfg.timer_count = 0; // 清除标记
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
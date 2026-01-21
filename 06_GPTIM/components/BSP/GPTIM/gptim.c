#include "gptim.h"
#include "esp_log.h"

// 对应旧版的 timer_group_isr_callback
static bool IRAM_ATTR timer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    timg_config_t *user_data = (timg_config_t *)user_ctx;
    
    // 记录触发时的计数值 (edata 中包含报警值)
    user_data->timer_count = edata->count_value;
    
    // 翻转 LED
    LED_TOGGLE();

    // 返回 true 表示需要唤醒高优先级任务
    return true; 
}

void timg_new_init(timg_config_t *user_config)
{
    // 1. 定时器基础配置
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT, // 默认时钟
        .direction = GPTIMER_COUNT_UP,      // 向上计数
        .resolution_hz = 1000000,           // 频率设为1MHz，即 1 tick = 1us
    };
    gptimer_handle_t gptimer = NULL;
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    // 2. 配置报警行为 (对应旧版的 alarm_value 和 auto_reload)
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = user_config->timing_us, // 报警目标值
        .reload_count = 0,                     // 重载值（通常为0）
        .flags.auto_reload_on_alarm = true,    // 开启自动重载
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    // 3. 注册回调函数 (ISR)
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_on_alarm_cb, // 报警回调
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, user_config));

    // 4. 使能并启动定时器
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));
}
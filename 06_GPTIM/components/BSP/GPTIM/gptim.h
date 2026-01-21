#ifndef __GPTIM_H__
#define __GPTIM_H__

#include "driver/gptimer.h"
#include "led.h"

typedef struct {
    uint64_t timing_us;    // 定时时间（微秒）
    uint64_t timer_count;  // 记录计数值
} timg_config_t;

// 修改初始化函数原型，返回句柄或直接在内部处理
void timg_new_init(timg_config_t *user_config);

#endif
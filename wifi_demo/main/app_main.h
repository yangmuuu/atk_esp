#pragma once
#include <stdint.h>

// XL9555输入中断回调，供main初始化使用
void app_xl9555_input_cb(uint16_t io_num, int level);

// 业务主入口
void app_entry(void);
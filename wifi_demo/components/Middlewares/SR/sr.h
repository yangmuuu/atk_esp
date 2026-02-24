#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// 唤醒回调函数类型
typedef void (*sr_wakeup_cb_t)(void);

// 初始化语音唤醒引擎
esp_err_t sr_wake_init(sr_wakeup_cb_t cb);

// 获取AFE每次处理的音频采样点数量
int sr_get_feed_chunk_size(void);

// 向唤醒模型输入音频数据
void sr_wakeup_feed(int16_t *audio_chunk);

// 暂停唤醒词检测
void sr_wake_suspend(void);

// 恢复唤醒词检测
void sr_wake_resume(void);
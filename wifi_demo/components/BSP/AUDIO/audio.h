#pragma once
#include "esp_err.h"

// 初始化扬声器 (默认 24000Hz，用于 AI 语音播放)
esp_err_t audio_init(void);

// 初始化麦克风 (默认 16000Hz，用于唤醒词和录音)
esp_err_t mic_init(void);

// 音频输出 (写入喇叭，len 为字节长度)
int audio_write(const void *data, int len);

// 音频输入 (读取麦克风，len 为字节长度)
int audio_read(void *data, int len);
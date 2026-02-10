#pragma once
#include "esp_err.h"

esp_err_t audio_init(void);
// 注意：这里的 len 是字节长度
int audio_write(const void *data, int len);
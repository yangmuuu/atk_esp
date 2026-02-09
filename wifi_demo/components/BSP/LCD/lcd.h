#pragma once

#include "lvgl.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LCD (ST7789) 和 触摸 (FT6X36)
 * @note  必须在 main 函数中先初始化 xl9555_init() 后才能调用此函数
 * @return lv_display_t* LVGL 显示对象句柄
 */
lv_display_t * bsp_lcd_init(void);

#ifdef __cplusplus
}
#endif
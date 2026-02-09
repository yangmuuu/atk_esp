#include "driver/gpio.h"
#include "lv_port.h"
#include "xl9555.h"
#include <stdio.h>
#include "lv_demos.h"
#include "esp_lvgl_port.h"

#define XL9555_SDA GPIO_NUM_10
#define XL9555_SCL GPIO_NUM_11

#define LCD_RST_IO IO1_3
#define LCD_BL_IO IO1_2

void app_main(void)
{
    xl9555_init(GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_NC, NULL);
    xl9555_ioconfig((~(LCD_RST_IO | LCD_BL_IO)) & 0xFFFF);
	xl9555_pin_write(LCD_BL_IO, 1); // 背光
    lv_port_init();

	lvgl_port_lock(0);
	lv_demo_widgets();
	lvgl_port_unlock();
}

#include "esp_event.h"
#include "esp_log.h"

#include "xl9555.h"
#include "lcd.h"
#include "wifi.h"
#include "camera.h"

#include "app_main.h"

// 日志标签
static const char *TAG = "MAIN";

// I2C引脚定义
#define XL9555_SDA GPIO_NUM_10
#define XL9555_SCL GPIO_NUM_11

// 程序入口
void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化IO扩展芯片 (注册业务层的回调)
    ESP_LOGI(TAG, "Hardware Init: XL9555");
    xl9555_init(XL9555_SDA, XL9555_SCL, GPIO_NUM_17, app_xl9555_input_cb);
    
    // 配置输出引脚:
    // IO1_3: LCD复位
    // IO1_2: LCD背光
    // IO1_0: 触摸复位
    // IO0_0: 喇叭功放使能 (本次新增)
    // 0xFFFF为全输入, 按位与操作清除对应位以设为输出
    xl9555_ioconfig(0xFFFF & ~(IO1_3 | IO1_2 | IO1_0 | IO0_0));

    // 默认关闭喇叭，防止上电瞬间产生爆音，等需要播放时由 audio 驱动打开
    xl9555_pin_write(IO0_0, 0);

    // 初始化屏幕与触摸
    ESP_LOGI(TAG, "Hardware Init: LCD");
    if (bsp_lcd_init() == NULL) {
        ESP_LOGE(TAG, "LCD init failed, halting system");
        return;
    }

    // 初始化WiFi
    ESP_LOGI(TAG, "Hardware Init: WiFi");
    wifi_init_portal();
    wifi_wait_for_ip();

    // 初始化摄像头
    ESP_LOGI(TAG, "Hardware Init: Camera");
    if (camera_init() == ESP_OK) {
        ESP_LOGI(TAG, "Camera ready");
    } else {
        ESP_LOGE(TAG, "Camera failed (non-blocking)");
    }

    // 硬件准备完毕，移交控制权给业务层
    app_entry();
}